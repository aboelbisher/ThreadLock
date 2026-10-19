// World 2's backdrop (DESIGN.md §5): the apothecary's back wall on market day, dark oiled planks with a
// warm haze of light from the shop window (top right). Drawn once into a still picture (apothecary_art.cpp)
// in two layers of this one shader: uLayer 0 = the wall, first; uLayer 1 = the vignette, last, over the
// shelves and the counter too. Positions are in the whole picture's units (p below), the tile starts at uOffset.
shader_type canvas;

uniform vec2 uCanvas;  // this tile's canvas size in units
uniform vec2 uOffset;  // where this tile starts in the whole picture (units)
uniform vec2 uTotal;   // the whole picture's size (units): one screen, or a tall board's column
uniform vec2 uScreen;  // one screen (units)
uniform float uScale;  // board units -> canvas units
uniform float uLayer;  // 0 = wall, 1 = vignette

const vec3 kWallTop = vec3(0.239, 0.173, 0.122);  // #3D2C1F
const vec3 kWallMid = vec3(0.180, 0.129, 0.090);  // #2E2117
const vec3 kWallLow = vec3(0.102, 0.071, 0.047);  // #1A120C
const vec3 kSeam = vec3(0.078, 0.051, 0.031);     // between the planks
const vec3 kHaze = vec3(1.0, 0.851, 0.627);       // #FFD9A0

float Cover(float d, float aa) {
  return clamp(0.5 - d / aa, 0.0, 1.0);
}

float Hash(float n) {
  return fract(sin(n * 127.1) * 43758.5453);
}

void fragment() {
  vec2 p = vUV * uCanvas + uOffset;
  if (uLayer > 0.5) {
    // Vignette: round one screen, which slides along a tall column (the ends stay dark).
    vec2 middle = vec2(uTotal.x * 0.5, clamp(p.y, uScreen.y * 0.47, uTotal.y - uScreen.y * 0.5));
    float to_centre = length((p - middle) * vec2(1.0, 0.82)) / (uScreen.x * 1.33);
    float dark = 0.6 * smoothstep(0.42, 1.0, to_centre);
    FragColor = vec4(0.0, 0.0, 0.0, dark);
    return;
  }
  float aa = uCanvas.x / max(uSize.x, 1.0);  // one pixel, in units
  float s = uScale;

  // The wall: warm at the top, darker below (by screen, so a tall board darkens towards its foot).
  float t = clamp(p.y / uTotal.y, 0.0, 1.0);
  vec3 col = t < 0.55 ? mix(kWallTop, kWallMid, t / 0.55) : mix(kWallMid, kWallLow, (t - 0.55) / 0.45);

  // Upright planks 46 board units wide, each a slightly different tone, with a faint grain along them.
  float width = 46.0 * s;
  float plank = floor(p.x / width);
  float within = p.x - plank * width;
  col *= 0.92 + 0.12 * Hash(plank + 3.0);
  float grain = sin(p.x / s * 1.7 + sin(p.y / s * 0.045 + plank * 2.3) * 3.0 + plank * 11.0);
  col *= 1.0 + 0.035 * grain;
  col = mix(col, kSeam, 0.75 * Cover(min(within, width - within) - 0.7 * s, aa));
  // A lighter edge on the left of each seam (the plank's bevel catches the light).
  col += vec3(0.05, 0.035, 0.02) * Cover(abs(within - 1.6 * s) - 0.5 * s, aa);

  // The haze: a broad soft band of window light from the top right, falling to the bottom left.
  vec2 a = vec2(uTotal.x * 1.05, uScreen.y * 0.02), b = vec2(-uTotal.x * 0.15, uScreen.y);
  vec2 ab = normalize(b - a);
  float across = abs(dot(p - a, vec2(-ab.y, ab.x)));
  float along = clamp(dot(p - a, ab) / length(b - a), 0.0, 1.0);
  float band = mix(40.0, 150.0, along) * s;
  col += kHaze * 0.075 * smoothstep(band, band * 0.15, across) * (1.0 - 0.6 * along);

  FragColor = vec4(col, 1.0);
}
