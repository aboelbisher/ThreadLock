// World 3's backdrop (DESIGN.md §5): the canal city at night, seen from above. Drawn once into a still picture
// (canal_art.cpp) in two layers of this one shader: uLayer 0 = the ground between the rooftops, first (a deep
// night blue with a faint haze of city light); uLayer 1 = the vignette, last, over the roofs and canals too.
// Positions are in the whole picture's units (p below), the tile starts at uOffset.
shader_type canvas;

uniform vec2 uCanvas;  // this tile's canvas size in units
uniform vec2 uOffset;  // where this tile starts in the whole picture (units)
uniform vec2 uTotal;   // the whole picture's size (units): one screen, or a tall board's column
uniform vec2 uScreen;  // one screen (units)
uniform float uScale;  // board units -> canvas units
uniform float uLayer;  // 0 = ground, 1 = vignette

const vec3 kGround = vec3(0.039, 0.063, 0.106);  // #0A101B
const vec3 kHaze = vec3(0.180, 0.260, 0.420);    // the glow of the city's lamps on the night air

float Hash(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void fragment() {
  vec2 p = vUV * uCanvas + uOffset;
  if (uLayer > 0.5) {
    // Vignette: round one screen, which slides along a tall column (the ends stay dark).
    vec2 middle = vec2(uTotal.x * 0.5, clamp(p.y, uScreen.y * 0.45, uTotal.y - uScreen.y * 0.55));
    float to_centre = length((p - middle) / uScreen) / 0.75;
    FragColor = vec4(0.0, 0.0, 0.0, 0.55 * smoothstep(0.55, 1.0, to_centre));
    return;
  }
  // The ground: a little lighter in slow broad patches (haze), with a faint grain so it doesn't band.
  vec2 q = p / (uScale * 160.0);
  float haze = 0.5 + 0.25 * sin(q.x * 2.1 + sin(q.y * 1.3) * 1.7) + 0.25 * sin(q.y * 1.7 + q.x * 0.6);
  vec3 col = kGround + kHaze * 0.05 * haze;
  col += (Hash(floor(p / max(uScale, 0.01))) - 0.5) * 0.008;
  FragColor = vec4(col, 1.0);
}
