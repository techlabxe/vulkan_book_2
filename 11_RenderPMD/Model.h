#pragma once
#include "VulkanAppBase.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Material
{
public:
  struct MaterialParameters {
    glm::vec4 diffuse;
    glm::vec4 ambient;
    glm::vec4 specular;
    glm::uvec1 useTexture;
    glm::uvec1 edgeFlag;
  };
  Material(const MaterialParameters& params) : m_parameters(params), m_uniformBuffer(), m_texture(){ }

  glm::vec4 GetDiffuse() const { return m_parameters.diffuse; }
  glm::vec4 GetAmbient() const { return m_parameters.ambient; }
  glm::vec4 GetSpecular() const { return m_parameters.specular; }
  bool GetEdgeFlag() const { return m_parameters.edgeFlag.x != 0; }

  void SetTexture(VulkanAppBase::ImageObject texture) { m_texture = texture; }
  void SetUniformBuffer(VulkanAppBase::BufferObject ubo) { m_uniformBuffer = ubo; }

  VulkanAppBase::ImageObject GetTexture() { return m_texture; }
  VulkanAppBase::BufferObject GetUniformBuffer() { return m_uniformBuffer; }
  bool HasTexture() const { return m_parameters.useTexture.x != 0; }
  void Update(VulkanAppBase* app);

  VkDescriptorSet GetDescriptorSet(int index) const { return m_descriptorSets[index]; }
  void SetDescriptorSet(std::vector<VkDescriptorSet> descriptorSets) { m_descriptorSets = descriptorSets; }

private:
  MaterialParameters m_parameters;
  VulkanAppBase::BufferObject m_uniformBuffer;
  VulkanAppBase::ImageObject  m_texture;
  std::vector<VkDescriptorSet> m_descriptorSets;
};

class Bone
{
public:
  Bone() : m_rotation(1.0f, 0.0f, 0.0f, 0.0f), m_parent(nullptr), m_mtxInvBind(1.0f){ }
  Bone(const std::string& name) : m_name(name), m_rotation(1.0f, 0.0f, 0.0f, 0.0f), m_parent(nullptr), m_mtxInvBind(1.0f) {  }

  void SetTranslation(const glm::vec3& trans) { m_translation = trans; }
  void SetRotation(const glm::quat& rot) { m_rotation = rot; }

  glm::vec3 GetTranslation() const { return m_translation; }
  glm::quat GetRotation() const { return m_rotation; }

  const std::string& GetName() const { return m_name; }
  glm::mat4 GetLocalMatrix() const { return m_mtxLocal; }
  glm::mat4 GetWorldMatrix() const { return m_mtxWorld; }
  glm::mat4 GetInvBindMatrix() const { return m_mtxInvBind; }

  void UpdateLocalMatrix();
  void UpdateWorldMatrix();
  void UpdateMatrices();

  void SetInitialTranslation(const glm::vec3& trans) { m_initialTranslation = trans; }

  void SetParent(Bone* parent)
  {
    m_parent = parent;
    if (m_parent != nullptr)
    {
      m_parent->AddChild(this);
    }
  }
  Bone* GetParent() const
  {
    return m_parent;
  }
  glm::vec3 GetInitialTranslation() const { return m_initialTranslation; }

  void SetInvBindMatrix(const glm::mat4& mtxInvBind) { m_mtxInvBind = mtxInvBind; }
private:
  void AddChild(Bone* bone) { m_children.push_back(bone); }
  std::string m_name;

  glm::vec3 m_translation;
  glm::quat m_rotation;
  glm::vec3 m_initialTranslation;

  Bone* m_parent;
  std::vector<Bone*> m_children;

  glm::mat4 m_mtxLocal;
  glm::mat4 m_mtxWorld;
  glm::mat4 m_mtxInvBind;
};
class PMDBoneIK
{
public:
  PMDBoneIK() : m_effector(nullptr), m_target(nullptr), m_angleLimit(0.0f), m_iteration(0) { }
  PMDBoneIK(Bone* target, Bone* eff) : m_effector(eff), m_target(target) { }

  Bone* GetEffector() const { return m_effector; }
  Bone* GetTarget() const { return m_target; }
  float GetAngleWeight() const { return m_angleLimit; }
  const std::vector<Bone*>& GetChains() const { return m_ikChains; }
  int GetIterationCount() const { return m_iteration; }

  void SetAngleLimit(float angle) { m_angleLimit = angle; }
  void SetIterationCount(int iterationCount) { m_iteration = iterationCount; }
  void SetIkChains(std::vector<Bone*>& chains) { m_ikChains = chains; }
private:
  Bone* m_effector;
  Bone* m_target;
  std::vector<Bone*> m_ikChains;
  float m_angleLimit;
  int   m_iteration;
};

class Model
{
public:
  using SecondaryCommandBuffers = std::vector<VkCommandBuffer>;

  void Load(const char* fileName, VulkanAppBase* app);
  void Prepare(VulkanAppBase* app);
  void Cleanup(VulkanAppBase* app);

  struct Mesh {
    uint32_t startIndexOffset;
    uint32_t indexCount;
  };

  struct PMDVertex
  {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::uvec2 boneIndices;
    glm::vec2 boneWeights;
    uint32_t  edgeFlag;
  };
  struct SceneParameter
  {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 lightDirection;
    glm::vec4 eyePosition;
    glm::vec4 outlineColor;
    glm::mat4 lightViewProj;
    glm::mat4 lightViewProjBias;
  };
  struct BoneParameter
  {
    glm::mat4 bone[512];
  };

  void SetSceneParameter(const SceneParameter& params) { m_sceneParams = params; }
  
  void UpdateMatrices();
  void Update(uint32_t imageIndex, VulkanAppBase* app);

  SecondaryCommandBuffers GetCommandBuffers(uint32_t index);
  SecondaryCommandBuffers GetCommandBuffersOutline(uint32_t index);
  SecondaryCommandBuffers GetCommandBuffersShadow(uint32_t index);

  void SetShadowMap(VulkanAppBase::ImageObject shadowMap) { m_shadowMap = shadowMap; }

  // ボーン情報
  uint32_t GetBoneCount() const { return uint32_t(m_bones.size()); }
  const Bone* GetBone(int idx) const { return m_bones[idx]; }
  Bone* GetBone(int idx) { return m_bones[idx]; }

  // 表情モーフ情報.
  uint32_t GetFaceMorphCount() const { return uint32_t(m_faceOffsetInfo.size()); }
  int GetFaceMorphIndex(const std::string& faceName) const;
  void SetFaceMorphWeight(int index, float weight);

  // IK情報
  uint32_t GetBoneIKCount() const { return uint32_t(m_boneIkList.size()); }
  const PMDBoneIK& GetBoneIK(int idx) const { return m_boneIkList[idx]; }

private:
  void PrepareModelUniformBuffers(uint32_t count, VulkanAppBase* app);
  void PreparePipelines(VulkanAppBase* app);
  void PrepareDescriptorSets(VulkanAppBase* app);
  void PrepareDummyTexture(VulkanAppBase* app);
  void PrepareCommandBuffers(uint32_t count, VulkanAppBase* app);

  std::vector<PMDVertex> m_hostMemVertices;
  std::vector<Mesh> m_meshes;
  std::vector<Material> m_materials;
  SceneParameter m_sceneParams;
  BoneParameter m_boneMatrices;

  using UniformBuffers = std::vector<VulkanAppBase::BufferObject>;

  std::vector<VulkanAppBase::BufferObject> m_vertexBuffers;
  UniformBuffers m_boneUBO;
  UniformBuffers m_sceneParamUBO;
  
  VulkanAppBase::BufferObject m_indexBuffer;

  std::vector<SecondaryCommandBuffers> m_commandBuffers;
  std::vector<SecondaryCommandBuffers> m_commandBuffersOutline;
  std::vector<SecondaryCommandBuffers> m_commandBuffersShadow;

  VulkanAppBase::ImageObject m_shadowMap;
  VulkanAppBase::ImageObject m_dummyTexture;
  VkSampler m_sampler;

  std::unordered_map<std::string, VkPipeline> m_pipelines;
  std::vector<Bone*> m_bones;
  

  // 表情モーフベース頂点情報.
  struct PMDFaceBaseInfo
  {
    std::vector<uint32_t> indices;
    std::vector<glm::vec3> verticesPos;
  } m_faceBaseInfo;
  
  // 表情モーフオフセット頂点情報.
  struct PMDFaceInfo
  {
    std::string name;
    std::vector<uint32_t> indices;
    std::vector<glm::vec3> verticesOffset;
  };
  std::vector<PMDFaceInfo> m_faceOffsetInfo;
  std::vector<float> m_faceMorphWeights;

  std::vector<PMDBoneIK> m_boneIkList;
};
