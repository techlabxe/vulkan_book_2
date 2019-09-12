#version 450

layout(location=0) in vec4 inPos;
layout(location=1) in vec3 inNormal;

layout(location=0) out vec4 outColor;
layout(location=1) out vec3 outNormal;
layout(location=2) out vec4 outWorldPos;

out gl_PerVertex
{
  vec4 gl_Position;
};


layout(set=0, binding=0)
uniform SceneParameters
{
  mat4  world;
  mat4  view;
  mat4  proj;
  vec4  lightPos;
  vec4  cameraPos;
};

void main()
{
  gl_Position = proj * view * world * inPos;
  
  vec3 worldNormal = mat3(world) * inNormal;
  float l = dot(worldNormal, vec3(0, 1,0)) * 0.5 + 0.5;
  outColor = vec4(l);
  outWorldPos = world * inPos;
  outNormal = worldNormal;
}
