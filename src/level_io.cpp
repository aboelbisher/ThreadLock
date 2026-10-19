#include "level_io.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <tuple>
#include <utility>

#include "apothecary_look.h"
#include "json.hpp"  // nlohmann::json (vendored in the engine's third_party/tinygltf)

namespace threadlock {
namespace {

using Json = nlohmann::ordered_json;

// Small readers that never throw: a level file with a wrong type reports an error instead.
struct Reader {
  std::vector<std::string>& errors;
  std::string where;

  void Error(const std::string& text) { errors.push_back(where + ": " + text); }

  std::string String(const Json& object, const char* key, const std::string& fallback = {}) {
    if (!object.contains(key)) return fallback;
    if (!object[key].is_string()) {
      Error(std::string("'") + key + "' should be text");
      return fallback;
    }
    return object[key].get<std::string>();
  }
  int Int(const Json& object, const char* key, int fallback) {
    if (!object.contains(key)) return fallback;
    if (!object[key].is_number_integer()) {
      Error(std::string("'") + key + "' should be a whole number");
      return fallback;
    }
    return object[key].get<int>();
  }
  bool Bool(const Json& object, const char* key, bool fallback) {
    if (!object.contains(key)) return fallback;
    if (!object[key].is_boolean()) {
      Error(std::string("'") + key + "' should be true / false");
      return fallback;
    }
    return object[key].get<bool>();
  }
  bool Point(const Json& value, Vec2& out) {
    if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
      Error("a point should be [x, y]");
      return false;
    }
    out = {value[0].get<float>(), value[1].get<float>()};
    return true;
  }
  bool Color(const Json& object, const char* key, threadlock::Color& out) {
    const std::string text = String(object, key);
    if (text.empty()) return false;
    if (!ParseColor(text, out)) {
      Error("unknown colour '" + text + "'");
      return false;
    }
    return true;
  }
};

bool ReadPiece(Reader& reader, const Json& value, Piece& out) {
  std::string type;
  if (value.is_string()) {
    type = value.get<std::string>();
  } else if (value.is_object()) {
    type = reader.String(value, "type");
  } else {
    reader.Error("a tray piece should be a name or an object");
    return false;
  }
  if (!ParsePieceType(type, out.type)) {
    reader.Error("unknown piece '" + type + "'");
    return false;
  }
  if (value.is_object()) {
    out.n = reader.Int(value, "n", 0);
    reader.Color(value, "a", out.a);
    reader.Color(value, "b", out.b);
  }
  return true;
}

bool ReadLevel(const Json& json, Level& level, std::vector<std::string>& errors) {
  Reader reader{errors, "level"};
  const size_t errors_before = errors.size();
  if (!json.is_object()) {
    reader.Error("should be an object");
    return false;
  }
  level = Level{};
  level.id = reader.String(json, "id");
  reader.where = "level " + level.id;
  level.name = reader.String(json, "name", level.id);
  level.intro = reader.String(json, "intro");
  if (json.contains("height")) {
    if (json["height"].is_number())
      level.look.height = json["height"].get<float>();
    else
      reader.Error("'height' should be a number");
    if (level.look.height < 600.0f) reader.Error("'height' should be at least 600");
  }
  if (json.contains("payoff")) reader.Point(json["payoff"], level.look.payoff);
  if (json.contains("shelves")) {
    if (!json["shelves"].is_array()) reader.Error("'shelves' should be a list of heights");
    for (const Json& value : json["shelves"]) {
      if (!value.is_number()) {
        reader.Error("a shelf is a height (y)");
        continue;
      }
      const float y = value.get<float>();
      if (y < 0.0f || y > level.look.height) reader.Error("a shelf is off the board");
      level.look.shelves.push_back(y);
    }
  }
  const std::string type = reader.String(json, "type", "build");
  if (!ParseLevelType(type, level.type)) reader.Error("unknown type '" + type + "'");
  if (level.id.empty()) reader.Error("no id");

  if (!json.contains("nodes") || !json["nodes"].is_array()) {
    reader.Error("no nodes");
    return false;
  }
  std::vector<std::string> twins;
  std::vector<std::vector<std::string>> gears;
  for (const Json& item : json["nodes"]) {
    Node node;
    node.id = reader.String(item, "id");
    const std::string kind = reader.String(item, "kind");
    if (!ParseNodeKind(kind, node.kind)) reader.Error("node '" + node.id + "': unknown kind '" + kind + "'");
    if (item.contains("at"))
      reader.Point(item["at"], node.at);
    else
      reader.Error("node '" + node.id + "' has no 'at'");
    node.socket = reader.Bool(item, "socket", node.kind == NodeKind::Pass);
    if (node.kind == NodeKind::Spout) {
      const std::string beads = reader.String(item, "beads");
      if (!ParseBeads(beads, node.beads)) reader.Error("spout '" + node.id + "': bad beads '" + beads + "'");
      node.beat = reader.Int(item, "beat", 12);
      node.start = reader.Int(item, "start", 1);
    }
    node.flap = reader.Int(item, "flap", 0);
    node.flap_tappable = reader.Bool(item, "tap", false);
    if (node.kind == NodeKind::Cup) {
      reader.Color(item, "want", node.want);
      node.count = reader.Int(item, "count", 0);
      node.order = reader.String(item, "order");
    }
    if (node.kind == NodeKind::Rack) {
      node.period = reader.Int(item, "period", 4);
      node.first = reader.Int(item, "first", 0);
      if (!item.contains("slots") || !item["slots"].is_array()) {
        reader.Error("rack '" + node.id + "' has no 'slots'");
      } else {
        for (const Json& value : item["slots"]) {
          if (!value.is_object()) {
            reader.Error("a rack's bottle should be {\"want\": ..., \"count\": n}");
            continue;
          }
          Slot slot;
          reader.Color(value, "want", slot.want);
          slot.count = reader.Int(value, "count", 0);
          slot.order = reader.String(value, "order");
          node.slots.push_back(std::move(slot));
        }
      }
    }
    if (node.kind == NodeKind::Gate) node.open = reader.Bool(item, "open", true);
    std::vector<std::string> geared;
    if (item.contains("gears")) {
      if (!item["gears"].is_array()) reader.Error("node '" + node.id + "': 'gears' should be a list of gates");
      for (const Json& value : item["gears"]) {
        if (value.is_string()) geared.push_back(value.get<std::string>());
      }
    }
    gears.push_back(std::move(geared));
    twins.push_back(reader.String(item, "twin"));
    level.nodes.push_back(std::move(node));
  }
  for (size_t i = 0; i < twins.size(); ++i) {
    if (twins[i].empty()) continue;
    const int other = level.NodeIndex(twins[i]);
    if (other < 0) {
      reader.Error("node '" + level.nodes[i].id + "': no twin '" + twins[i] + "'");
      continue;
    }
    level.nodes[i].twin = other;
    level.nodes[static_cast<size_t>(other)].twin = static_cast<int>(i);
  }
  for (size_t i = 0; i < gears.size(); ++i) {
    for (const std::string& gate : gears[i]) {
      const int at = level.NodeIndex(gate);
      if (at < 0) {
        reader.Error("node '" + level.nodes[i].id + "': no gate '" + gate + "'");
        continue;
      }
      level.nodes[i].gears.push_back(at);
    }
  }

  if (!json.contains("pipes") || !json["pipes"].is_array()) {
    reader.Error("no pipes");
    return false;
  }
  for (const Json& item : json["pipes"]) {
    if (!item.is_array() || item.size() < 2 || !item[0].is_string() || !item[1].is_string()) {
      reader.Error("a pipe should be [from, to, [bends]]");
      continue;
    }
    Pipe pipe;
    pipe.from = level.NodeIndex(item[0].get<std::string>());
    pipe.to = level.NodeIndex(item[1].get<std::string>());
    if (pipe.from < 0 || pipe.to < 0) {
      reader.Error("pipe " + item[0].get<std::string>() + " -> " + item[1].get<std::string>() + ": unknown node");
      continue;
    }
    if (item.size() >= 3) {
      if (!item[2].is_array()) {
        reader.Error("a pipe's bends should be a list of points");
      } else {
        for (const Json& point : item[2]) {
          Vec2 p;
          if (reader.Point(point, p)) pipe.via.push_back(p);
        }
      }
    }
    if (item.size() >= 4) {
      // Shapes: {"coil": [segment, turns], "drip": segment}
      const Json& shape = item[3];
      if (!shape.is_object()) {
        reader.Error("a pipe's shapes should be an object");
      } else {
        if (shape.contains("coil")) {
          const Json& coil = shape["coil"];
          if (coil.is_array() && coil.size() == 2 && coil[0].is_number_integer() && coil[1].is_number_integer()) {
            pipe.coil_segment = coil[0].get<int>();
            pipe.coil_turns = coil[1].get<int>();
          } else {
            reader.Error("a coil is [segment, turns]");
          }
        }
        if (shape.contains("drip")) {
          if (shape["drip"].is_number_integer()) {
            pipe.drip_segment = shape["drip"].get<int>();
          } else {
            reader.Error("a drip is the number of a segment");
          }
        }
      }
    }
    level.pipes.push_back(std::move(pipe));
  }

  if (json.contains("tray")) {
    if (!json["tray"].is_array())
      reader.Error("'tray' should be a list");
    else {
      for (const Json& item : json["tray"]) {
        TrayPiece tray_piece;
        if (ReadPiece(reader, item, tray_piece.piece)) level.tray.push_back(tray_piece);
      }
    }
  }
  if (json.contains("placed")) {
    if (!json["placed"].is_array())
      reader.Error("'placed' should be a list");
    else {
      for (const Json& item : json["placed"]) {
        TrayPiece tray_piece;
        if (!item.is_object() || !item.contains("piece") || !ReadPiece(reader, item["piece"], tray_piece.piece)) {
          reader.Error("a placed piece should be {\"piece\": ..., \"at\": node}");
          continue;
        }
        const std::string at = reader.String(item, "at");
        tray_piece.at = level.NodeIndex(at);
        if (tray_piece.at < 0) reader.Error("placed piece on unknown node '" + at + "'");
        tray_piece.setting = reader.Int(item, "set", 0);
        tray_piece.locked = reader.Bool(item, "locked", false);
        level.tray.push_back(tray_piece);
      }
    }
  }
  if (errors.size() != errors_before) return false;
  const std::vector<std::string> problems = level.Finalize();
  errors.insert(errors.end(), problems.begin(), problems.end());
  return problems.empty();
}

bool ReadPlacement(const Json& json, const Level& level, Placement& out, std::vector<std::string>& errors) {
  out = {};
  out.at.assign(level.tray.size(), -1);
  out.setting.assign(level.tray.size(), 0);
  out.flap.assign(level.nodes.size(), -1);
  for (size_t i = 0; i < level.tray.size(); ++i) {
    out.setting[i] = level.tray[i].setting;
    if (level.tray[i].locked) out.at[i] = level.tray[i].at;
  }
  for (size_t n = 0; n < level.nodes.size(); ++n) {
    if (level.nodes[n].kind == NodeKind::Fork && level.nodes[n].flap_tappable) out.flap[n] = level.nodes[n].flap;
  }
  if (!json.is_object()) {
    errors.push_back(level.id + ": a placement should be an object");
    return false;
  }
  if (json.contains("at") && json["at"].is_array()) {
    for (size_t i = 0; i < json["at"].size() && i < out.at.size(); ++i) {
      const Json& at = json["at"][i];
      if (at.is_string()) {
        out.at[i] = level.NodeIndex(at.get<std::string>());
        if (out.at[i] < 0) errors.push_back(level.id + ": a placement names unknown node " + at.get<std::string>());
      }
    }
  }
  if (json.contains("set") && json["set"].is_array()) {
    for (size_t i = 0; i < json["set"].size() && i < out.setting.size(); ++i) {
      if (json["set"][i].is_number_integer()) out.setting[i] = json["set"][i].get<int>();
    }
  }
  if (json.contains("flaps") && json["flaps"].is_object()) {
    for (const auto& [key, value] : json["flaps"].items()) {
      const int node = level.NodeIndex(key);
      if (node >= 0 && value.is_number_integer()) out.flap[static_cast<size_t>(node)] = value.get<int>();
    }
  }
  return true;
}

Json PlacementJson(const Level& level, const Placement& placement) {
  Json json = Json::object();
  Json at = Json::array(), set = Json::array();
  for (size_t i = 0; i < placement.at.size(); ++i) {
    at.push_back(placement.at[i] >= 0 ? Json(level.nodes[static_cast<size_t>(placement.at[i])].id) : Json(nullptr));
    set.push_back(i < placement.setting.size() ? placement.setting[i] : 0);
  }
  json["at"] = at;
  json["set"] = set;
  Json flaps = Json::object();
  for (size_t n = 0; n < placement.flap.size(); ++n) {
    if (placement.flap[n] >= 0) flaps[level.nodes[n].id] = placement.flap[n];
  }
  if (!flaps.empty()) json["flaps"] = flaps;
  return json;
}

}  // namespace

bool ParseLevel(std::string_view text, Level& out, std::vector<std::string>& errors) {
  const Json json = Json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
  if (json.is_discarded()) {
    errors.push_back("not valid JSON");
    return false;
  }
  try {
    return ReadLevel(json, out, errors);
  } catch (const std::exception& e) {
    errors.push_back(std::string("level: ") + e.what());
    return false;
  }
}

bool ParseWorld(std::string_view text, World& out, std::vector<std::string>& errors) {
  const Json json = Json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
  if (json.is_discarded() || !json.is_object()) {
    errors.push_back("a world file should be a JSON object");
    return false;
  }
  try {
    Reader reader{errors, "world"};
    out = World{};
    out.number = reader.Int(json, "world", 0);
    out.name = reader.String(json, "name");
    reader.Color(json, "accent", out.accent);
    if (!json.contains("levels") || !json["levels"].is_array()) {
      reader.Error("no levels");
      return false;
    }
    bool ok = true;
    int number = 0;
    for (const Json& item : json["levels"]) {
      Level level;
      ok &= ReadLevel(item, level, errors);
      level.world = out.number;
      level.number = level.type == LevelType::Challenge ? 0 : ++number;
      out.levels.push_back(std::move(level));
    }
    return ok;
  } catch (const std::exception& e) {
    errors.push_back(std::string("world: ") + e.what());
    return false;
  }
}

bool ApplyPars(std::string_view text, std::vector<World>& worlds, std::vector<std::string>& errors) {
  const Json json = Json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
  if (json.is_discarded() || !json.is_object()) {
    errors.push_back("the pars file should be a JSON object");
    return false;
  }
  try {
    for (World& world : worlds) {
      for (Level& level : world.levels) {
        if (!json.contains(level.id)) continue;
        const Json& item = json[level.id];
        Reader reader{errors, "pars " + level.id};
        level.pars.pieces = reader.Int(item, "pieces", -1);
        level.pars.ticks = reader.Int(item, "ticks", -1);
        level.pars.solutions = reader.Int(item, "solutions", 0);
        if (item.contains("lean")) ReadPlacement(item["lean"], level, level.pars.lean, errors);
        if (item.contains("swift")) ReadPlacement(item["swift"], level, level.pars.swift, errors);
      }
    }
    return true;
  } catch (const std::exception& e) {
    errors.push_back(std::string("pars: ") + e.what());
    return false;
  }
}

std::string WritePars(const std::vector<const Level*>& levels) {
  std::vector<const Level*> sorted = levels;
  auto key = [](const Level* level) {
    return std::make_tuple(level->world, level->type == LevelType::Challenge ? 1 : 0, level->number, level->id);
  };
  std::sort(sorted.begin(), sorted.end(), [&](const Level* a, const Level* b) { return key(a) < key(b); });
  std::string out = "{\n";
  for (size_t i = 0; i < sorted.size(); ++i) {
    const Level& level = *sorted[i];
    Json item = Json::object();
    item["pieces"] = level.pars.pieces;
    item["ticks"] = level.pars.ticks;
    item["solutions"] = level.pars.solutions;
    item["lean"] = PlacementJson(level, level.pars.lean);
    item["swift"] = PlacementJson(level, level.pars.swift);
    out += "  \"" + level.id + "\": " + item.dump() + (i + 1 < sorted.size() ? ",\n" : "\n");
  }
  out += "}\n";
  return out;
}

std::string WriteLevel(const Level& level) {
  auto number = [](float v) {
    const float rounded = std::round(v);
    return std::fabs(rounded - v) < 0.01f ? std::to_string(static_cast<int>(rounded)) : std::to_string(v);
  };
  auto point = [&](Vec2 p) { return "[" + number(p.x) + ", " + number(p.y) + "]"; };
  auto text = [](const std::string& s) { return Json(s).dump(); };  // quoted, escaped
  std::string out = "{\n  \"id\": " + text(level.id) + ", \"name\": " + text(level.name);
  if (level.type != LevelType::Build) out += std::string(", \"type\": \"") + LevelTypeKey(level.type) + "\"";
  if (!level.intro.empty()) out += ", \"intro\": " + text(level.intro);
  const Level::LevelLook look_default;
  if (level.look.height != look_default.height) out += ", \"height\": " + number(level.look.height);
  if (!(level.look.payoff == look_default.payoff)) out += ", \"payoff\": " + point(level.look.payoff);
  if (!level.look.shelves.empty()) {
    out += ", \"shelves\": [";
    for (size_t s = 0; s < level.look.shelves.size(); ++s) out += (s ? ", " : "") + number(level.look.shelves[s]);
    out += "]";
  }
  out += ",\n  \"nodes\": [\n";
  for (size_t i = 0; i < level.nodes.size(); ++i) {
    const Node& n = level.nodes[i];
    out += "    {\"id\": " + text(n.id) + ", \"kind\": \"" + NodeKindKey(n.kind) + "\", \"at\": " + point(n.at);
    if (n.kind == NodeKind::Spout) {
      out += ", \"beads\": \"" + BeadsText(n.beads) + "\", \"beat\": " + std::to_string(n.beat);
      if (n.start != 1) out += ", \"start\": " + std::to_string(n.start);
    }
    if (n.kind == NodeKind::Cup) {
      out += std::string(", \"want\": \"") + ColorLetter(n.want) + "\", \"count\": " + std::to_string(n.count);
      if (!n.order.empty()) out += ", \"order\": " + text(n.order);
    }
    if (n.kind == NodeKind::Rack) {
      out += ", \"period\": " + std::to_string(n.period);
      if (n.first != 0) out += ", \"first\": " + std::to_string(n.first);
      out += ", \"slots\": [";
      for (size_t s = 0; s < n.slots.size(); ++s) {
        out += std::string(s ? ", " : "") + "{\"want\": \"" + ColorLetter(n.slots[s].want) +
               "\", \"count\": " + std::to_string(n.slots[s].count);
        if (!n.slots[s].order.empty()) out += ", \"order\": " + text(n.slots[s].order);
        out += "}";
      }
      out += "]";
    }
    if (!n.gears.empty()) {
      out += ", \"gears\": [";
      for (size_t g = 0; g < n.gears.size(); ++g)
        out += (g ? ", " : "") + text(level.nodes[static_cast<size_t>(n.gears[g])].id);
      out += "]";
    }
    if (n.kind == NodeKind::Gate && !n.open) out += ", \"open\": false";
    if (n.kind != NodeKind::Pass && n.socket) out += ", \"socket\": true";
    if (n.kind == NodeKind::Fork && n.flap != 0) out += ", \"flap\": " + std::to_string(n.flap);
    if (n.flap_tappable) out += ", \"tap\": true";
    if (n.twin > static_cast<int>(i)) out += ", \"twin\": " + text(level.nodes[static_cast<size_t>(n.twin)].id);
    out += i + 1 < level.nodes.size() ? "},\n" : "}\n";
  }
  out += "  ],\n  \"pipes\": [";
  for (size_t i = 0; i < level.pipes.size(); ++i) {
    const Pipe& p = level.pipes[i];
    out += "[\"" + level.nodes[static_cast<size_t>(p.from)].id + "\", \"" + level.nodes[static_cast<size_t>(p.to)].id +
           "\"";
    const bool shaped = p.coil_segment >= 0 || p.drip_segment >= 0;
    if (!p.via.empty() || shaped) {
      out += ", [";
      for (size_t k = 0; k < p.via.size(); ++k) out += (k ? ", " : "") + point(p.via[k]);
      out += "]";
    }
    if (shaped) {
      out += ", {";
      if (p.coil_segment >= 0)
        out += "\"coil\": [" + std::to_string(p.coil_segment) + ", " + std::to_string(p.coil_turns) + "]";
      if (p.drip_segment >= 0)
        out += std::string(p.coil_segment >= 0 ? ", " : "") + "\"drip\": " + std::to_string(p.drip_segment);
      out += "}";
    }
    out += i + 1 < level.pipes.size() ? "], " : "]";
  }
  out += "],\n  \"tray\": [";
  bool first = true;
  std::string placed;
  for (const TrayPiece& piece : level.tray) {
    std::string spec = std::string("\"") + PieceKey(piece.piece.type) + "\"";
    if (piece.piece.type == PieceType::Overflow) {
      spec = "{\"type\": \"overflow\", \"n\": " + std::to_string(piece.piece.n) + "}";
    }
    if (piece.piece.type == PieceType::Separator && (piece.piece.a != Color::Blue || piece.piece.b != Color::Amber)) {
      spec = std::string("{\"type\": \"separator\", \"a\": \"") + ColorLetter(piece.piece.a) + "\", \"b\": \"" +
             ColorLetter(piece.piece.b) + "\"}";
    }
    if (piece.at >= 0) {
      if (!placed.empty()) placed += ", ";
      placed += "{\"piece\": " + spec + ", \"at\": \"" + level.nodes[static_cast<size_t>(piece.at)].id + "\"" +
                (piece.setting ? ", \"set\": " + std::to_string(piece.setting) : std::string()) +
                (piece.locked ? ", \"locked\": true" : "") + "}";
      continue;
    }
    out += (first ? "" : ", ") + spec;
    first = false;
  }
  out += "]";
  if (!placed.empty()) out += ",\n  \"placed\": [" + placed + "]";
  out += "\n}";
  return out;
}

std::string PlacementText(const Level& level, const Placement& placement) {
  std::string text;
  for (size_t i = 0; i < placement.at.size() && i < level.tray.size(); ++i) {
    if (placement.at[i] < 0 || level.tray[i].locked) continue;
    if (!text.empty()) text += ", ";
    const PieceType type = level.tray[i].piece.type;
    text += PieceKey(type);
    if (type == PieceType::Overflow) text += std::to_string(level.tray[i].piece.n);
    text += "@" + level.nodes[static_cast<size_t>(placement.at[i])].id;
    if (HasSetting(type) && i < placement.setting.size()) text += "/" + std::to_string(placement.setting[i]);
  }
  for (size_t n = 0; n < placement.flap.size() && n < level.nodes.size(); ++n) {
    if (placement.flap[n] < 0) continue;
    if (!text.empty()) text += ", ";
    text += "flap@" + level.nodes[n].id + "/" + std::to_string(placement.flap[n]);
  }
  return text.empty() ? "(nothing)" : text;
}

std::vector<std::string> LookProblems(const Level& level) {
  std::vector<std::string> problems;
  for (const Node& node : level.nodes) {
    if (node.at.x < 0 || node.at.x > 360 || node.at.y < 0 || node.at.y > level.look.height)
      problems.push_back("node " + node.id + " is off the board");
  }
  if (!level.look.shelves.empty()) {
    // The copper lines of a shop wall: every stretch from node to bend to node runs across or down.
    for (const Pipe& pipe : level.pipes) {
      std::vector<Vec2> points{level.nodes[static_cast<size_t>(pipe.from)].at};
      points.insert(points.end(), pipe.via.begin(), pipe.via.end());
      points.push_back(level.nodes[static_cast<size_t>(pipe.to)].at);
      for (size_t k = 0; k + 1 < points.size(); ++k) {
        if (points[k].x != points[k + 1].x && points[k].y != points[k + 1].y) {
          problems.push_back("pipe " + level.nodes[static_cast<size_t>(pipe.from)].id + " -> " +
                             level.nodes[static_cast<size_t>(pipe.to)].id + " runs at a slant (segment " +
                             std::to_string(k) + ")");
          break;
        }
      }
    }
    // The order slips hang on a cork board in the top right, the bell beside it: nothing of the machine
    // behind them.
    const int slips = static_cast<int>(apothecary::Orders(level).size());
    const float cork_top = apothecary::CorkTop(level);
    const apothecary::Box cork = apothecary::CorkBoard(slips, cork_top), bell = apothecary::BellBox(slips, cork_top);
    for (const Node& node : level.nodes) {
      if (cork.Contains(node.at)) problems.push_back("node " + node.id + " is under the cork board");
      if (bell.Contains(node.at)) problems.push_back("node " + node.id + " is under the bell");
    }
    // Each spout's jar and its slate: on the board, clear of the machine and of each other.
    auto off_board = [&](const apothecary::Box& box) {
      return box.min.x < 0.0f || box.max.x > 360.0f || box.min.y < 0.0f || box.max.y > level.look.height;
    };
    std::vector<std::pair<std::string, apothecary::Box>> walls;  // what hangs on the wall so far
    for (size_t s = 0; s < level.nodes.size(); ++s) {
      const Node& spout = level.nodes[s];
      if (spout.kind != NodeKind::Spout) continue;
      const apothecary::Jar jar = apothecary::JarAt(level, static_cast<int>(s));
      const std::pair<std::string, apothecary::Box> parts[] = {{"the jar of spout " + spout.id, jar.whole},
                                                               {"the slate of spout " + spout.id, jar.slate}};
      for (const auto& [name, box] : parts) {
        if (off_board(box)) problems.push_back(name + " is off the board");
        for (const Node& node : level.nodes) {
          if (&node != &spout && box.Contains(node.at)) problems.push_back("node " + node.id + " is under " + name);
        }
        for (const auto& [other, earlier] : walls) {
          if (box.Overlaps(earlier)) problems.push_back(name + " overlaps " + other);
        }
        if (box.Overlaps(cork) || box.Overlaps(bell)) problems.push_back(name + " overlaps the cork board or bell");
      }
      walls.insert(walls.end(), std::begin(parts), std::end(parts));
    }
    // The bottles: on the counter, filled from above through a nozzle, each for an order.
    const float counter = apothecary::CounterTop(level);
    for (const Node& node : level.nodes) {
      if (node.kind != NodeKind::Cup) continue;
      const float feet = node.at.y + apothecary::kBottleBase;
      if (feet < counter || feet > counter + apothecary::kCounterFace)
        problems.push_back("bottle " + node.id + " isn't on the counter");
      if (node.order.empty()) problems.push_back("bottle " + node.id + " has no order");
    }
    for (const Pipe& pipe : level.pipes) {
      const Node& to = level.nodes[static_cast<size_t>(pipe.to)];
      if (to.kind != NodeKind::Cup && to.kind != NodeKind::Rack) continue;
      const Vec2 last = pipe.via.empty() ? level.nodes[static_cast<size_t>(pipe.from)].at : pipe.via.back();
      if (last.x != to.at.x || last.y >= to.at.y)
        problems.push_back("pipe " + level.nodes[static_cast<size_t>(pipe.from)].id + " -> " + to.id +
                           (to.kind == NodeKind::Cup ? " doesn't come down into the bottle"
                                                     : " doesn't come down into the rack's tap"));
    }
    // The racks: lazy susans of two to four bottles, each for an order, on the counter with room to turn
    // (clear of the bottles, the sinks and each other), their clocks clear of the bottles' tags.
    for (size_t r = 0; r < level.nodes.size(); ++r) {
      const Node& rack = level.nodes[r];
      if (rack.kind != NodeKind::Rack) continue;
      const std::string name = "rack " + rack.id;
      if (rack.slots.size() < 2 || rack.slots.size() > 4) problems.push_back(name + " needs 2 to 4 bottles");
      for (size_t k = 0; k < rack.slots.size(); ++k) {
        if (rack.slots[k].order.empty())
          problems.push_back(name + "'s bottle " + std::to_string(k + 1) + " has no order");
      }
      const float top = rack.at.y + apothecary::kRackBase - apothecary::kRackLip;  // as the counter's top
      if (top < counter - apothecary::kBottleLip || top > counter + apothecary::kCounterFace - apothecary::kBottleLip)
        problems.push_back(name + " isn't on the counter");
      const apothecary::Box table = apothecary::RackBox(rack);
      if (off_board(table)) problems.push_back(name + "'s turntable is off the board");
      const Vec2 clock = apothecary::ClockAt(rack);
      const apothecary::Box dial{{clock.x - apothecary::kClockRadius, clock.y - apothecary::kClockRadius},
                                 {clock.x + apothecary::kClockRadius, clock.y + apothecary::kClockRadius}};
      for (size_t n = 0; n < level.nodes.size(); ++n) {
        const Node& node = level.nodes[n];
        apothecary::Box box;
        if (node.kind == NodeKind::Cup) {
          box = {{node.at.x - 14.0f, node.at.y}, {node.at.x + 14.0f, node.at.y + apothecary::kBottleBase}};
          if (dial.Overlaps(apothecary::BottleTag(node, counter)))
            problems.push_back(name + "'s clock is over bottle " + node.id + "'s tag");
        }
        if (node.kind == NodeKind::Drain)
          box = {{node.at.x - 27.0f, node.at.y - 7.0f}, {node.at.x + 27.0f, node.at.y + 7.0f}};
        if (node.kind == NodeKind::Rack && n > r) box = apothecary::RackBox(node);
        if (table.Overlaps(box)) problems.push_back(name + "'s turntable runs into " + node.id);
      }
    }
  }
  return problems;
}

}  // namespace threadlock
