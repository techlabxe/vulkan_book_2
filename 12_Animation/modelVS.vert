#version 450

layout(location=0) in vec4 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in uvec2 inBlendIndices;
layout(location=4) in vec2 inBlendWeights;
layout(location=5) in uint inEdgeFlag;

layout(location=0) out vec4 outColor;
layout(location=1) out vec2 outUV;
layout(location=2) out vec3 outNormal;
layout(location=3) out vec4 outWorldPosition;
layout(location=4) out vec4 outShadowPosition;
layout(location=5) out vec4 outShadowPosUV;

out gl_PerVertex
{
  vec4 gl_Position;
};


layout(set=0, binding=0)
uniform SceneParameter
{
  mat4  view;
  mat4  proj;
  vec4  lightDirection;
  vec4  eyePosition;
  vec4  outlineColor;
  mat4  lightViewProj;
  mat4  lightViewProjBias;
};

layout(set=0, binding=1)
uniform BoneParameter
{
  mat4 boneMatrices[512];
};

vec4 TransformPosition( vec4 position)
{
  vec4 pos = vec4(0);
  for( int i=0;i<2;++i)
  {
    mat4 mtx = boneMatrices[ inBlendIndices[i] ];
	pos += (mtx * position) * inBlendWeights[i];
  }
  return pos;
}
vec3 TransformNormal()
{
  vec3 nrm = vec3(0);
  for( int i=0;i<2;++i)
  {
    mat4 mtx = boneMatrices[ inBlendIndices[i] ];
	nrm += (mat3(mtx) * inNormal) * inBlendWeights[i];
  }
  return normalize(nrm);
}

void main()
{
  mat4 matPV = proj * view;
  vec4 worldPos = TransformPosition(inPosition);
  gl_Position = matPV * worldPos;
  vec3 worldNormal = TransformNormal();

  float l = dot(worldNormal, vec3(0, 1,0)) * 0.5 + 0.5;
  outColor = vec4(1);
  outUV = inUV;
  outNormal = worldNormal;
  outWorldPosition = worldPos;

  outShadowPosition = lightViewProj * worldPos;
  outShadowPosUV = lightViewProjBias * worldPos;
}
