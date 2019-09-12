#include "Animator.h"
#include <fstream>

#include "loader/PMDloader.h"

#include "Model.h"

using namespace std;
using namespace glm;

static float dFx(float ax, float ay, float t)
{
  float s = 1.0f - t;
  float v = -6.0f * s * t * t * ax + 3.0f * s * s * ax - 3.0f * t * t * ay + 6.0f * s * t * ay + 3.0f * t * t;
  return v;
}
static float fx(float ax, float ay, float t, float x0)
{
  float s = 1.0f - t;
  return 3.0f * s * s * t * ax + 3.0f * s * t * t * ay + t * t * t - x0;
}

static float funcBezierX(glm::vec4 k, float t)
{
  float s = 1.0f - t;
  return 3.0f * s * s * t * k.x + 3.0f * s * t * t * k.y + t * t * t;
}
static float funcBezierY(glm::vec4 k, float t)
{
  float s = 1.0f - t;
  return 3.0f * s * s * t * k.y + 3.0f * s * t * t * k.w + t * t * t;
}


void Animator::Prepare(const char* filename)
{
  std::ifstream infile(filename, std::ios::binary);
  loader::VMDFile loader(infile);

  m_framePeriod = loader.getKeyframeCount();
  uint32_t nodeCount = loader.getNodeCount();
  for (uint32_t i = 0; i < nodeCount; ++i)
  {
    const auto& name = loader.getNodeName(i);
    m_nodeMap[name] = NodeAnimation();
    auto& keyframes = m_nodeMap[name];
    auto framesSrc = loader.getKeyframes(name);

    auto frameCount = uint32_t(framesSrc.size());
    std::vector<NodeAnimeFrame> frames(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i)
    {
      auto& dst = frames[i];
      auto& src = framesSrc[i];
      dst.frame = src.getKeyframeNumber();
      dst.translation = src.getLocation();
      dst.rotation = src.getRotation();
      dst.interpX = src.getBezierParam(0);
      dst.interpY = src.getBezierParam(1);
      dst.interpZ = src.getBezierParam(2);
      dst.interpR = src.getBezierParam(3);
    }
    keyframes.SetKeyframes(frames);
  }

  uint32_t morphCount = loader.getMorphCount();
  for (uint32_t i = 0; i < morphCount; ++i)
  {
    const auto& name = loader.getMorphName(i);
    m_morphMap[name] = MorphAnimation();
    auto& keyframes = m_morphMap[name];
    auto framesSrc = loader.getMorphKeyframes(name);
    
    auto frameCount = uint32_t(framesSrc.size());
    std::vector<MorphAnimeFrame> frames(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i)
    {
      auto& dst = frames[i];
      const auto& src = framesSrc[i];
      dst.frame = src.getKeyframeNumber();
      dst.weight = src.getWeight();
    }
    keyframes.SetKeyframes(frames);
  }
}

void Animator::Cleanup()
{
}

void Animator::UpdateAnimation(uint32_t animeFrame)
{
  if (m_model == nullptr)
  {
    return;
  }

  // 全てのノードで指定されたフレームでの値を計算する.
  UpdateNodeAnimation(animeFrame);
  UpdateMorthAnimation(animeFrame);

  UpdateIKchains();
}

void Animator::UpdateNodeAnimation(uint32_t animeFrame)
{
  auto boneCount = m_model->GetBoneCount();
  for (uint32_t i = 0; i < boneCount; ++i) {
    auto bone = m_model->GetBone(i);

    auto itr = m_nodeMap.find(bone->GetName());
    if (itr == m_nodeMap.end())
    {
      continue;
    }
    auto segment = (*itr).second.FindSegment(animeFrame);
    auto start = std::get<0>(segment);
    auto last = std::get<1>(segment);

    // 線形補間.
    auto range = float(last.frame - start.frame);
    vec3 translation = start.translation;
    quat rotation = start.rotation;
    if (range > 0)
    {
      auto rate = float(animeFrame - start.frame) / float(range);
      translation = start.translation;
      translation += (last.translation - start.translation) * rate;
      translation += bone->GetInitialTranslation();
      rotation = normalize(glm::slerp(start.rotation, last.rotation, rate));
    }
    if (range == 0)
    {
      continue;
    }
    bone->SetTranslation(translation);
    bone->SetRotation(rotation);

#if 01
    auto rate = float(animeFrame - start.frame) / float(range);
    vec4 bezierK(0.f);
    bezierK.x = InterporateBezier(start.interpX, rate);
    bezierK.y = InterporateBezier(start.interpY, rate);
    bezierK.z = InterporateBezier(start.interpZ, rate);
    bezierK.w = InterporateBezier(start.interpR, rate);

    translation = start.translation;
    translation += (last.translation - start.translation) * vec3(bezierK);
    translation += bone->GetInitialTranslation();
    bone->SetTranslation(translation);

    rotation = glm::slerp(start.rotation, last.rotation, bezierK.w);
    bone->SetRotation(rotation);
#endif
  }
  // 各ボーンの姿勢をセットしたので行列を更新.
  m_model->UpdateMatrices();
}
float Animator::InterporateBezier(const glm::vec4& bezier, float x)
{
  float t = 0.5f;
  float ft = fx(bezier.x, bezier.z, t, x);
  for (int i = 0; i < 32; ++i)
  {
    auto dfx = dFx(bezier.x, bezier.z, t);
    t = t - ft / dfx;
    ft = fx(bezier.x, bezier.z, t, x);
  }
  t = std::min(std::max(0.0f, t), 1.0f);
  float dy = funcBezierY(bezier, t);
  return dy;
}

void Animator::UpdateMorthAnimation(uint32_t animeFrame)
{
  for (auto& m : m_morphMap)
  {
    auto name = m.first;
    auto segment = m.second.FindSegment(animeFrame);
    auto start = std::get<0>(segment);
    auto last = std::get<1>(segment);

    auto range = float(last.frame - start.frame);
    auto weight = start.weight;
    if (range > 0)
    {
      auto rate = float(animeFrame - start.frame) / range;
      weight += (last.weight - start.weight) * rate;
    }

    auto index = m_model->GetFaceMorphIndex(name);
    m_model->SetFaceMorphWeight(index, weight);
  }
}

void Animator::Attach(Model* model)
{
  m_model = model;
}

void Animator::UpdateIKchains()
{
  if (m_model == nullptr)
  {
    return;
  }
  auto ikCount = m_model->GetBoneIKCount();
  for (uint32_t i = 0; i < ikCount; ++i)
  {
    SolveIK(m_model->GetBoneIK(i));
  }
}

inline glm::vec4 GetPosition(const glm::mat4& m)
{
  return glm::vec4(m[3]);
}

void Animator::SolveIK(const PMDBoneIK& boneIk)
{
  auto target = boneIk.GetTarget();
  auto eff = boneIk.GetEffector();

  const auto& chains = boneIk.GetChains();
  for (int ite = 0; ite < boneIk.GetIterationCount(); ++ite)
  {
    for (uint32_t i = 0; i < chains.size(); ++i)
    {
      auto bone = chains[i];
      auto mtxInvBone = glm::inverse(bone->GetWorldMatrix());

      // エフェクタとターゲットの位置を、現在ボーンでのローカル空間にする.
      auto effectorPos = vec3(mtxInvBone * GetPosition(eff->GetWorldMatrix()));
      auto targetPos = vec3(mtxInvBone * GetPosition(target->GetWorldMatrix()));

      auto len = glm::length(targetPos - effectorPos);
      if (len * len < 0.0001f)
      {
        return;
      }
      // 現ボーンよりターゲットおよびエフェクタへ向かうベクトルを生成.
      auto vecToEff = glm::normalize(effectorPos);
      auto vecToTarget = glm::normalize(targetPos);

      auto dot = glm::dot(vecToEff, vecToTarget);
      dot = glm::clamp(dot, -1.0f, 1.0f);
      float radian = acosf(dot);
      if (radian < 0.0001f)
        continue;
      auto limitAngle = boneIk.GetAngleWeight();
      radian = glm::clamp(radian, -limitAngle, limitAngle);

      // 回転軸を求める.
      auto axis = normalize(cross(vecToTarget, vecToEff));

      if (radian < 0.001f)
      {
        continue;
      }

      if (bone->GetName().find("ひざ") != std::string::npos )
      {
        auto rotation = angleAxis(radian, axis);
        auto eulerAngle = eulerAngles(rotation);
        eulerAngle.y = 0.0f; eulerAngle.z = 0.0f;
        eulerAngle.x = clamp(eulerAngle.x, 0.002f, glm::pi<float>());
        rotation = quat(eulerAngle);

        rotation = normalize(bone->GetRotation() * rotation);
        bone->SetRotation(rotation);
      }
      else
      {
        auto rotation = angleAxis(radian, axis);
        rotation = normalize(bone->GetRotation() * rotation);
        bone->SetRotation(rotation);
      }
      
      // 位置座標更新.
      for (int j = i; j >= 0; --j)
      {
        chains[j]->UpdateWorldMatrix();
      }
      eff->UpdateWorldMatrix();
      target->UpdateWorldMatrix();
    }
  }
}
