#include "feedback.h"

#include <algorithm>

#include "wlw/core/log.h"
#include "wlw/platform/platform.h"

namespace threadlock {

void Feedback::Load(wlw::audio::Audio& audio) {
  audio_ = &audio;
  const uint32_t rate = audio.GetMixer().SampleRate();
  for (size_t i = 0; i < ids_.size(); ++i) {
    const std::vector<float> mono = synth::Make(static_cast<synth::Sound>(i), rate);
    wlw::audio::SoundData data;
    data.frames = static_cast<uint32_t>(mono.size());
    data.samples.resize(mono.size() * 2);
    for (size_t k = 0; k < mono.size(); ++k) data.samples[2 * k] = data.samples[2 * k + 1] = mono[k];
    ids_[i] = audio.GetMixer().AddSound(std::move(data));
  }
  audio.GetMixer().max_instances_per_sound = 3;
  WLW_LOG_INFO("Synthesized %zu sounds at %u Hz", ids_.size(), rate);
}

void Feedback::Play(synth::Sound sound, float volume, float pitch, float pan) {
  if (!sound_on || !audio_) return;
  audio_->Play(ids_[static_cast<size_t>(sound)],
               {.volume = volume, .pitch = pitch, .pan = std::clamp(pan, -1.0f, 1.0f)});
}

void Feedback::Vibrate(int milliseconds, float strength) {
  if (haptics_on) wlw::platform::Vibrate(milliseconds, strength);
}

void Feedback::OnEvents(const std::vector<Event>& events, const Level& level, const Board& board) {
  auto pan_of = [&](int node) {
    return node >= 0 ? (level.nodes[static_cast<size_t>(node)].at.x - 180.0f) / 180.0f * 0.6f : 0.0f;
  };
  bool landed = false;
  for (const Event& e : events) {
    switch (e.type) {
      case EventType::Land: {
        const Node& node = level.nodes[static_cast<size_t>(e.node)];
        if (node.kind != NodeKind::Cup) break;
        const bool pure = e.bead.a == node.want && (!e.bead.IsBlend() || e.bead.b == node.want);
        if (!pure) {
          Play(synth::Sound::Splash, 0.8f, 1.0f, pan_of(e.node));
        } else {
          const int step = std::max(0, e.value - e.bead.Drops());
          Play(synth::NoteFor(node.want), 0.55f, synth::ClimbPitch(step), pan_of(e.node));
        }
        landed = true;
        break;
      }
      case EventType::CupFull: {
        const Node& node = level.nodes[static_cast<size_t>(e.node)];
        Play(synth::ChordFor(node.want), 0.6f, 1.0f, pan_of(e.node));
        break;
      }
      case EventType::Collide:
        Play(synth::Sound::Thud, 0.9f, 1.0f, pan_of(e.node));
        Vibrate(35, 0.85f);
        break;
      case EventType::NearMiss: Play(synth::Sound::Chime, 0.5f, 1.0f, pan_of(e.node)); break;
      case EventType::Spill: Play(synth::Sound::Splash, 0.7f, 0.8f, pan_of(e.node)); break;
      case EventType::Jammed:
        Play(synth::Sound::Grind, 0.9f);
        Vibrate(70, 1.0f);
        break;
      case EventType::Pour: Play(synth::Sound::Pour, 0.22f, 1.0f, pan_of(e.node)); break;
      case EventType::Mix: Play(synth::Sound::NoteGreen, 0.45f, 0.75f, pan_of(e.node)); break;
      case EventType::Enter: {
        const int piece = board.PieceAt(e.node);
        if (piece < 0) break;
        const PieceType type = board.PieceInfo(piece).piece.type;
        if (type == PieceType::Turnstile || type == PieceType::Flipper || type == PieceType::Overflow) {
          Play(synth::Sound::Click, 0.45f, type == PieceType::Flipper ? 0.85f : 1.0f, pan_of(e.node));
        }
        break;
      }
      default: break;
    }
  }
  if (landed) Vibrate(8, 0.25f);
}

}  // namespace threadlock
