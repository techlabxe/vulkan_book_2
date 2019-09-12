#version 450

layout(location=0) in vec4 inPos;
layout(location=1) in vec3 inNormal;

layout(location=0) out vec4 outColor;

out gl_PerVertex
{
  vec4 gl_Position;
};


layout(set=0, binding=0)
uniform SceneParameters
{
  mat4  view;
  mat4  proj;
};

struct InstanceData
{
  mat4 world;
  vec4 color;
};

layout(set=0, binding=1)
uniform InstanceParameters
{
  InstanceData data[200];
};

void main()
{
  mat4 world = data[gl_InstanceIndex].world;
  vec4 color = data[gl_InstanceIndex].color;
  gl_Position = proj * view * world * inPos;
  
  vec3 worldNormal = mat3(world) * inNormal;
  float l = dot(worldNormal, vec3(0, 1,0)) * 0.5 + 0.5;

  outColor.xyz = vec3(l) * color.xyz;
  outColor.w = color.w;
}
