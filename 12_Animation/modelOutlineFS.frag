#version 450

layout(location=0) out vec4 outColor;


layout(set=0, binding=0)
uniform SceneParameter
{
  mat4  view;
  mat4  proj;
  vec4  lightDirection;
  vec4  eyePosition;
  vec4  outlineColor;
};

layout(set=0, binding=2)
uniform MaterialParameter
{
  vec4 diffuse;
  vec4 ambient;
  vec4 specular;
  uint useTexture;
};

layout(set=0, binding=3)
uniform sampler2D diffuseTex;

void main()
{
  outColor = vec4(outlineColor.rgb, 1);
}
