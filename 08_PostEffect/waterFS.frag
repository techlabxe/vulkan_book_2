#version 450
layout(location=0) in vec2 inUV;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0)
uniform EffectParameter
{
  vec2 windowSize;
  float blockSize;
  uint frameCount;
  float ripple;
  float speed;
  float distortion;
  float brightness;
};

layout(set=0, binding=1)
uniform sampler2D texRendered;

#define Iterations 8

void main()
{
  vec2 uv = inUV;
  float time = frameCount * 0.01;
  vec2 pos = gl_FragCoord.xy / windowSize * 12.0 - 20.0;
  vec2 tmp = pos;
  float speed2 = speed * 2.0;
  float inten  = 0.015;
  float col = 0;

  for (int i = 0; i < Iterations; ++i)
  {
    float t = time * (1.0 - (3.2 / (float(i) + speed)));
    tmp = pos + vec2(
      cos(t - tmp.x * ripple) + sin(t + tmp.y * ripple),
      sin(t - tmp.y * ripple) + cos(t + tmp.x * ripple)
      );
    tmp += time;
    col += 1.0 / length(vec2(pos.x / (sin(tmp.x + t * speed2) / inten), pos.y / (cos(tmp.y + t * speed2) / inten)));
  }
  col /= float(Iterations);
  col = clamp( 1.5 - sqrt(col), 0, 1.0 ) ;
  uv += col * distortion;

  outColor = texture(texRendered, uv);
  outColor += col * brightness;
}
