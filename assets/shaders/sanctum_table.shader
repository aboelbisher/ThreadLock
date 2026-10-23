// World 4's backdrop (DESIGN.md §5): the alchemists' obsidian table by candlelight, seen from above. Drawn once into
// a still picture (sanctum_art.cpp) in two layers of this one shader: uLayer 0 = the table, first (a violet-black
// radial gradient round the sigil, with a faint grain so it doesn't band); uLayer 1 = the vignette, last, over the
// inlay, grooves and glass too. Positions are in the whole picture's units (p below), the tile starts at uOffset.
shader_type canvas;

uniform vec2 uCanvas;  // this tile's canvas size in units
uniform vec2 uOffset;  // where this tile starts in the whole picture (units)
uniform vec2 uTotal;   // the whole picture's size (units): one screen, or a tall board's column
uniform vec2 uScreen;  // one screen (units)
uniform float uScale;  // board units -> canvas units
uniform float uLayer;  // 0 = table, 1 = vignette

const vec3 kCentre = vec3(0.114, 0.086, 0.161);  // #1D1629
const vec3 kMiddle = vec3(0.063, 0.047, 0.090);  // #100C17
const vec3 kEdge = vec3(0.027, 0.024, 0.043);    // #07060B

float Hash(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void fragment() {
  vec2 p = vUV * uCanvas + uOffset;
  // Round one screen's middle, which slides along a tall column (the ends stay dark).
  vec2 middle = vec2(uTotal.x * 0.5, clamp(p.y, uScreen.y * 0.45, uTotal.y - uScreen.y * 0.55));
  float d = length(p - middle) / (520.0 * uScale);
  if (uLayer > 0.5) {
    FragColor = vec4(0.0, 0.0, 0.0, 0.7 * smoothstep(0.5, 1.0, d));
    return;
  }
  vec3 col = mix(kCentre, kMiddle, smoothstep(0.0, 0.55, d));
  col = mix(col, kEdge, smoothstep(0.55, 1.0, d));
  col += (Hash(floor(p / max(uScale, 0.01))) - 0.5) * 0.008;
  FragColor = vec4(col, 1.0);
}
