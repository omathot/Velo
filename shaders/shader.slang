struct UniformBufferObject {
  float4x4 model;
  float4x4 view;
  float4x4 proj;
};

struct PushConstants {
  int objIdx;
  uint textureIdx;
};

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(1, 0)]]
Sampler2D textures[];

[[vk::binding(0, 0)]]
ConstantBuffer<UniformBufferObject> ubos[];

struct VSInput {
  float3 inPos;
  float3 inColor;
  float2 inTexCoord;
  uint32_t matidx;
};

struct VSOutput {
  float4 pos : SV_POSITION;
  float3 fragColor;
  float2 fragTexCoord;
  uint32_t matIdx;
};


[shader("vertex")]
VSOutput vertMain(VSInput input) {
  VSOutput output;
  UniformBufferObject ubo = ubos[pc.objIdx];

  output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPos, 1.0))));
  output.fragColor = input.inColor;
  output.fragTexCoord = input.inTexCoord;
  output.matIdx = input.matidx;
  return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_Target {
  // default (not codam)
  // return textures[NonUniformResourceIndex(pc.textureIdx)].Sample(vertIn.fragTexCoord);
  // default (codam)
  return textures[NonUniformResourceIndex(vertIn.matIdx)].Sample(vertIn.fragTexCoord);


  // fun effects
  // repeat
  // return textures[NonUniformResourceIndex(pc.textureIdx)].Sample(vertIn.fragTexCoord * 3);

  // color + repeat
  // return float4(vertIn.fragColor * textures[NonUniformResourceIndex(pc.textureIdx)].Sample(vertIn.fragTexCoord).rgb, 1.0);
}
