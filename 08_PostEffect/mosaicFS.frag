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

void main()
{
  vec2 uv = inUV * windowSize;
  uv /= blockSize;
  uv = floor(uv) * blockSize;
  uv /= windowSize.xy;

  outColor = texture(texRendered, uv);
}
