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
  float2 inPos;
  float3 inColor;
  float2 inTexCoord;;
};

struct VSOutput {
  float4 pos : SV_POSITION;
  float3 fragColor;
  float2 fragTexCoord;
};


[shader("vertex")]
VSOutput vertMain(VSInput input) {
  VSOutput output;
  // Access UBO by index from push constants
  UniformBufferObject ubo = ubos[pc.objIdx];

  output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPos, 0.0, 1.0))));
  output.fragColor = input.inColor;
  output.fragTexCoord = input.inTexCoord;
  return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_Target {
   return textures[pc.textureIdx].Sample(vertIn.fragTexCoord);
  // return textures[NonUniformResourceIndex(pc.textureIdx)].Sample(vertIn.fragTexCoord);
}
