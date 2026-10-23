// World 1's backdrop (DESIGN.md §5): the stone wall of the glassworks at dawn, a haze of light, and the
// great rose window, whose panes light up as the lanterns fill. Drawn once into a still picture
// (lantern_art.cpp), so it can afford to be a little rich. A tall board's still picture is several tiles
// (screens) of one wall: positions are in the whole picture's units (p below), the tile starts at uOffset.
shader_type canvas;

uniform vec2 uCanvas;       // this tile's canvas size in units
uniform vec2 uOffset;       // where this tile starts in the whole picture (units)
uniform vec2 uTotal;        // the whole picture's size (units): one screen, or a tall board's column
uniform vec2 uScreen;       // one screen (units)
uniform vec3 uWindow;       // the window's centre (units) and outer radius (units); radius 0: no window (the Lab)
uniform sampler2D uPanes;   // 24 x 1: rgb = a pane's light, a = how lit (0-11 outer, 12-23 inner)
uniform vec4 uBeam;         // the light falling from the window: rgb, strength

const float kTau = 6.2831853;
const vec3 kWallTop = vec3(0.098, 0.149, 0.231);  // #19263B
const vec3 kWallMid = vec3(0.067, 0.102, 0.169);  // #111A2B
const vec3 kWallLow = vec3(0.031, 0.047, 0.082);  // #080C15
const vec3 kLead = vec3(0.027, 0.039, 0.071);     // #070A12: the window's lead lines

// Coverage of a shape whose signed distance is `d` (negative inside), anti-aliased over `aa`.
float Cover(float d, float aa) {
  return clamp(0.5 - d / aa, 0.0, 1.0);
}

vec4 Pane(float index) {
  return texture(uPanes, vec2((index + 0.5) / 24.0, 0.5));
}

vec3 OuterPaneColor(float i) {
  float k = mod(i, 4.0);
  if (k < 0.5) return vec3(0.106, 0.176, 0.388);  // #1B2D63
  if (k < 1.5) return vec3(0.306, 0.208, 0.063);  // #4E3510
  if (k < 2.5) return vec3(0.067, 0.227, 0.251);  // #113A40
  return vec3(0.259, 0.090, 0.227);               // #42173A
}

vec3 Window(vec3 col, vec2 p, float aa_units) {
  vec2 d = (p - uWindow.xy) * (174.0 / uWindow.z);  // in the drawing's units: outer radius 174
  float aa = aa_units * (174.0 / uWindow.z);
  float r = length(d);
  if (r > 180.0) return col;
  float turn = fract(atan(d.x, -d.y) / kTau);  // 0 = up, clockwise
  float sector = floor(turn * 12.0);
  float within = turn * 12.0 - sector;  // 0..1 across the sector

  // Frame: a thin outer ring, a broad stone ring, the dark glass behind everything.
  col = mix(col, vec3(0.227, 0.278, 0.376), Cover(abs(r - 174.0) - 1.0, aa));
  col = mix(col, vec3(0.157, 0.196, 0.271), Cover(abs(r - 165.0) - 8.0, aa));
  col = mix(col, vec3(0.047, 0.078, 0.188), Cover(r - 157.0, aa));

  // Outer panes: twelve, in four colours; lit ones glow with their lantern's colour.
  float outer = Cover(abs(r - 127.0) - 29.0, aa);
  vec4 lit = Pane(sector);
  float centre_glow = 1.0 - abs(r - 127.0) / 29.0;
  vec3 dark = OuterPaneColor(sector) * 0.72;  // unlit glass stays in the background
  vec3 bright = lit.rgb * (0.82 + 0.35 * centre_glow);
  col = mix(col, mix(dark, bright, lit.a), outer);

  // Inner panes: twelve, blue and gold; they light when the machine is solved.
  float inner = Cover(abs(r - 70.0) - 25.0, aa);
  vec4 lit_inner = Pane(12.0 + sector);
  vec3 dark_inner = (mod(sector, 2.0) < 0.5 ? vec3(0.078, 0.141, 0.290) : vec3(0.227, 0.180, 0.071)) * 0.75;
  col = mix(col, mix(dark_inner, lit_inner.rgb * (0.85 + 0.3 * (1.0 - abs(r - 70.0) / 25.0)), lit_inner.a), inner);

  // The heart: amber glass with a rosette of six rings.
  if (r < 46.0) {
    float heart_lit = 0.0;
    for (int i = 0; i < 12; ++i) heart_lit += Pane(12.0 + float(i)).a;
    heart_lit /= 12.0;
    col = mix(col, mix(vec3(0.180, 0.149, 0.063), vec3(1.0, 0.86, 0.55), heart_lit), Cover(r - 43.0, aa));
    for (int k = 0; k < 6; ++k) {
      float a = float(k) * kTau / 6.0;
      vec2 c = 20.0 * vec2(sin(a), -cos(a));
      col = mix(col, kLead, Cover(abs(length(d - c) - 12.0) - 1.5, aa));
    }
  }

  // Lead: twelve spokes, three rings, and a roundel in each outer pane.
  float to_spoke = min(within, 1.0 - within) * kTau / 12.0;
  if (r > 44.0 && r < 157.0) col = mix(col, kLead, Cover(r * sin(to_spoke) - 2.25, aa));
  col = mix(col, kLead, Cover(abs(r - 157.0) - 3.0, aa));
  col = mix(col, kLead, Cover(abs(r - 98.0) - 2.5, aa));
  col = mix(col, kLead, Cover(abs(r - 44.0) - 2.5, aa));
  float roundel_angle = (sector + 0.5) * kTau / 12.0;
  vec2 roundel = 127.0 * vec2(sin(roundel_angle), -cos(roundel_angle));
  float to_roundel = length(d - roundel);
  col = mix(col, col + vec3(0.05), Cover(to_roundel - 11.0, aa));
  col = mix(col, kLead, Cover(abs(to_roundel - 11.0) - 1.5, aa));
  return col;
}

void fragment() {
  vec2 p = vUV * uCanvas + uOffset;
  float aa = uCanvas.x / max(uSize.x, 1.0);  // one pixel, in units

  // The wall: dusk-blue at the top, darker below.
  float t = p.y / uTotal.y;
  vec3 col = t < 0.5 ? mix(kWallTop, kWallMid, t * 2.0) : mix(kWallMid, kWallLow, t * 2.0 - 1.0);

  // Its stones: courses 14 high, blocks 56 long, every other course shifted by half a block.
  float course = floor(p.y / 14.0);
  float x = p.x + mod(course, 2.0) * 28.0;
  float to_joint = min(min(mod(x, 56.0), 56.0 - mod(x, 56.0)), min(mod(p.y, 14.0), 14.0 - mod(p.y, 14.0)));
  col += vec3(0.663, 0.745, 0.855) * 0.05 * Cover(to_joint - 0.6, aa);

  // Dawn haze: a broad band of pale light falling from the top right.
  vec2 a = vec2(uTotal.x * 0.82, 0.0), b = vec2(uTotal.x * 0.05, uTotal.y);
  vec2 ab = normalize(b - a);
  float across = abs(dot(p - a, vec2(-ab.y, ab.x)));
  float width = mix(50.0, 120.0, t);
  col += vec3(0.81, 0.90, 1.0) * 0.055 * smoothstep(width, width * 0.2, across);

  if (uWindow.z > 0.0) col = Window(col, p, aa);

  // The window's light falling into the room: a soft wedge below it, the colour of its lit panes.
  vec2 from = uWindow.xy + vec2(0.0, uWindow.z * 0.3);
  vec2 dir = normalize(vec2(-0.22, 1.0));
  float along = dot(p - from, dir);
  float side = abs(dot(p - from, vec2(-dir.y, dir.x)));
  float spread = uWindow.z * 0.35 + along * 0.22;
  float beam = smoothstep(spread, spread * 0.25, side) * smoothstep(0.0, uWindow.z * 0.6, along) *
               (1.0 - smoothstep(uScreen.y * 0.3, uScreen.y * 0.9, along));
  if (uWindow.z > 0.0) col += uBeam.rgb * uBeam.a * 0.12 * beam;

  // Vignette: round one screen, which slides along a tall column (the ends stay dark).
  vec2 middle = vec2(uTotal.x * 0.5, clamp(p.y, uScreen.y * 0.5, uTotal.y - uScreen.y * 0.5));
  float to_centre = length(p - middle) / (uScreen.x * 1.33);
  col *= 1.0 - 0.72 * clamp((to_centre - 0.5) / 0.5, 0.0, 1.0);

  FragColor = vec4(col, 1.0);
}
