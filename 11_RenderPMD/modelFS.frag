#version 450

layout(location=0) in vec4 inColor;
layout(location=1) in vec2 inUV;
layout(location=2) in vec3 inNormal;
layout(location=3) in vec4 inWorldPosition;
layout(location=4) in vec4 inShadowPosition;
layout(location=5) in vec4 inShadowPosUV;

layout(location=0) out vec4 outColor;


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

layout(set=0, binding=4)
uniform sampler2D shadowTex;

void main()
{
  vec4 color = diffuse;
  vec3 normal = normalize(inNormal);
  vec3 toLightDirection = normalize(lightDirection.xyz);
  float lmb = clamp( dot(toLightDirection, normalize(inNormal)), 0, 1);

  if( useTexture != 0)
  {
	color *= texture( diffuseTex, inUV.xy);
  }
  vec3 baseColor = color.xyz;
  color.rgb = baseColor * lmb;
  color.rgb += baseColor * ambient.xyz;

  vec3 toEyeDirection = normalize(eyePosition.xyz - inWorldPosition.xyz);
  vec3 halfVec = normalize(toEyeDirection + toLightDirection);
  float spc = pow(clamp(dot(normal, halfVec), 0, 1), specular.w);
  color.xyz += spc * specular.xyz;

  outColor = color;

  float z = inShadowPosition.z /inShadowPosition.w;
  vec4 fetchUV = inShadowPosUV / inShadowPosUV.w;
  float depthFromLight = texture(shadowTex, fetchUV.xy).r+0.002;
  if( depthFromLight < z )
  {
    // in shadow
	outColor.rgb *= 0.5;
	
  }
}
