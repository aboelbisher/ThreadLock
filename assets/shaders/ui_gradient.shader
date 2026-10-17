// A rounded rectangle filled with a vertical gradient: the big EDIT / PLAY button, tinted per world.
shader_type canvas;

uniform vec4 uTop;       // straight alpha
uniform vec4 uBottom;
uniform float uRadius;   // corner radius, in pixels

void fragment() {
  vec2 p = (vUV - 0.5) * uSize;
  vec2 q = abs(p) - uSize * 0.5 + uRadius;
  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - uRadius;
  float cover = clamp(0.5 - d, 0.0, 1.0);
  vec4 c = mix(uTop, uBottom, smoothstep(0.0, 1.0, vUV.y)) * vColor;
  FragColor = vec4(c.rgb * c.a, c.a) * cover;
}
