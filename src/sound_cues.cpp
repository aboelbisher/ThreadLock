#include "sound_cues.h"

#include <algorithm>

#include "rose_window.h"

namespace threadlock {
namespace {

using synth::Sound;
using synth::SoundSet;

float PanAt(float x) { return std::clamp((x - 180.0f) / 180.0f * 0.6f, -1.0f, 1.0f); }

// How far along its pipe a drop at `loc` is drawn (the middle of its cell); -1 off that pipe.
float DistanceOn(const Level& level, const Loc& loc, int pipe) {
  if (loc.kind != Loc::OnPipe || loc.index != pipe) return -1.0f;
  const Pipe& p = level.pipes[static_cast<size_t>(pipe)];
  return (static_cast<float>(loc.cell) + 0.5f) * p.length / static_cast<float>(p.cells);
}

}  // namespace

float ListenerGain(const Listener& listener, float y) {
  const float outside = std::max({listener.top - y, y - (listener.top + listener.visible), 0.0f});
  const float t = std::clamp(outside / std::max(listener.visible, 1.0f), 0.0f, 1.0f);
  return 1.0f + (0.35f - 1.0f) * t;
}

Cues CuesFor(const std::vector<Event>& events, const Level& level, const Board& board, SoundSet set,
             const Listener& listener) {
  Cues cues;
  const bool lantern = set == SoundSet::Lantern, canal = set == SoundSet::Canal;
  auto node_at = [&](int node) { return level.nodes[static_cast<size_t>(node)].at; };
  // A sound at a node: panned by its x, softer when it's off the view (failures are always heard in full).
  auto play = [&](Sound sound, float volume, float pitch, int node, bool failure = false) {
    if (node < 0) {
      cues.sounds.push_back({sound, volume, pitch, 0.0f});
      return;
    }
    const Vec2 at = node_at(node);
    const float gain = failure ? 1.0f : ListenerGain(listener, at.y);
    cues.sounds.push_back({sound, volume * gain, pitch, PanAt(at.x)});
  };
  // What a cup (or a rack's bottle) wants.
  auto want_of = [&](const Event& e) {
    const Node& node = level.nodes[static_cast<size_t>(e.node)];
    return node.kind == NodeKind::Rack && e.slot >= 0 ? node.slots[static_cast<size_t>(e.slot)].want : node.want;
  };
  bool landed = false;
  for (const Event& e : events) {
    switch (e.type) {
      case EventType::Land: {
        const Node& node = level.nodes[static_cast<size_t>(e.node)];
        if (node.kind != NodeKind::Cup && node.kind != NodeKind::Rack) break;
        const Color want = want_of(e);
        const bool pure = e.bead.a == want && (!e.bead.IsBlend() || e.bead.b == want);
        landed = true;
        if (!pure) {
          play(Sound::Splash, 0.8f, 1.0f, e.node, true);
          break;
        }
        const int before = std::max(0, e.value - e.bead.Drops());
        play(synth::NoteFor(set, want), 0.55f, synth::ClimbPitch(before), e.node);
        if (lantern && node.kind == NodeKind::Cup) {
          // The panes this drop lights, each a chime one step further round the window.
          const PaneRange panes = LanternPanes(level, e.node);
          const bool impure = board.Started() && board.Cup(e.node).impure;
          const int was = PanesLit(before, node.count, panes.Count(), impure);
          const int now = PanesLit(e.value, node.count, panes.Count(), impure);
          for (int p = was; p < now; ++p) play(Sound::PaneChime, 0.4f, synth::ClimbPitch(panes.first + p), e.node);
        }
        break;
      }
      case EventType::CupFull: play(synth::ChordFor(set, want_of(e)), 0.6f, 1.0f, e.node); break;
      case EventType::Flip:
        // A canal gate clanks when its leaves swing: the tick after the mill's flip (canal_art, GateOpenness).
        if (canal) {
          play(synth::GateFor(set), 0.6f, e.value ? 1.12f : 0.84f, e.node);
          cues.sounds.back().ticks_later = 1;
        } else {
          play(synth::GateFor(set), 0.55f, e.value ? 1.25f : 0.7f, e.node);
        }
        break;
      case EventType::Turn: play(synth::TurnFor(set), 0.3f, set == SoundSet::Shop ? 1.0f : 0.6f, e.node); break;
      case EventType::Leave:
        if (level.nodes[static_cast<size_t>(e.node)].kind == NodeKind::Mill) {  // the wheel turns a notch
          play(synth::MillFor(set), canal ? 0.5f : 0.4f, canal ? 1.0f : 0.5f, e.node);
        }
        break;
      case EventType::Collide:
        play(synth::MudFor(set), 0.9f, 1.0f, e.node, true);
        cues.buzzes.push_back({35, 0.85f});
        break;
      case EventType::NearMiss: play(Sound::Chime, 0.5f, 1.0f, e.node); break;
      case EventType::Spill: {
        const Sound sound = synth::SpillFor(set);
        const bool splash = sound == Sound::Splash;
        play(sound, splash ? 0.7f : 0.75f, splash ? 0.8f : 1.0f, e.node, true);
        break;
      }
      case EventType::Finished: {
        // A cup still short at the end (the shop's ghost fill and unpaid slip, the canal's empty berths) sounds
        // once for the run.
        const Sound sound = synth::ShortFor(set);
        if (sound == Sound::Count || e.value) break;
        int short_at = -1;
        for (size_t n = 0; n < level.nodes.size() && short_at < 0; ++n) {
          const Node& node = level.nodes[n];
          const int id = static_cast<int>(n);
          if (node.kind == NodeKind::Cup && board.Cup(id).count < node.count) short_at = id;
          for (size_t s = 0; node.kind == NodeKind::Rack && s < node.slots.size(); ++s) {
            if (board.Bottle(id, static_cast<int>(s)).count < node.slots[s].count) short_at = id;
          }
        }
        if (short_at >= 0) play(sound, 0.8f, 1.0f, short_at, true);
        break;
      }
      case EventType::Jammed:
        play(synth::JamFor(set), 0.9f, 1.0f, -1, true);
        cues.buzzes.push_back({70, 1.0f});
        break;
      case EventType::Pour: play(synth::PourFor(set), 0.22f, 1.0f, e.node); break;
      case EventType::Mix: play(synth::NoteFor(set, Color::Green), 0.45f, 0.75f, e.node); break;
      case EventType::Enter: {
        const int piece = board.PieceAt(e.node);
        if (piece < 0 || board.RoleOf(piece) != PieceRole::Acts) break;  // an idle piece stays still and silent
        const PieceType type = board.PieceInfo(piece).piece.type;
        if (type == PieceType::Turnstile || type == PieceType::Flipper || type == PieceType::Overflow) {
          play(Sound::Click, 0.45f, type == PieceType::Flipper ? 0.85f : 1.0f, e.node);
        }
        break;
      }
      default: break;
    }
  }
  if (landed) cues.buzzes.push_back({8, 0.25f});

  // Drip falls: a drop whose drawn place passed the funnel this tick plinks once (its pitch varies by drop).
  if (lantern) {
    auto plink = [&](const Pipe& pipe, int drop_id) {
      const Vec2 at = PointAlong(pipe.points, pipe.drip_end);
      const float pitch = 0.85f + 0.075f * static_cast<float>((drop_id * 7) % 5);
      cues.sounds.push_back({Sound::Drip, 0.25f * ListenerGain(listener, at.y), pitch, PanAt(at.x)});
    };
    auto dripping = [](const Pipe& pipe) { return pipe.drip_end > pipe.drip_start; };
    // Inside a pipe: the cell centre passed the funnel.
    for (const Drop& drop : board.Drops()) {
      if (!drop.alive || drop.loc.kind != Loc::OnPipe || drop.loc == drop.prev) continue;
      const Pipe& pipe = level.pipes[static_cast<size_t>(drop.loc.index)];
      const float was = DistanceOn(level, drop.prev, drop.loc.index), now = DistanceOn(level, drop.loc, drop.loc.index);
      if (dripping(pipe) && was < pipe.drip_end && pipe.drip_end <= now) plink(pipe, drop.id);
    }
    // Past the last cell's centre (a funnel at the pipe's end, often right over a lantern): as it leaves.
    for (const Event& e : events) {
      if (e.type != EventType::Enter || e.pipe < 0) continue;
      const Pipe& pipe = level.pipes[static_cast<size_t>(e.pipe)];
      const float last = DistanceOn(level, {Loc::OnPipe, e.pipe, pipe.cells - 1}, e.pipe);
      if (dripping(pipe) && pipe.drip_end > last) plink(pipe, e.drop);
    }
  }
  return cues;
}

std::vector<Cue> CueDelay::Tick(const std::vector<Cue>& cues) {
  std::vector<Cue> now;
  std::erase_if(waiting_, [&](Cue& cue) {
    if (--cue.ticks_later > 0) return false;
    now.push_back(cue);
    return true;
  });
  for (const Cue& cue : cues) (cue.ticks_later > 0 ? waiting_ : now).push_back(cue);
  return now;
}

}  // namespace threadlock
