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
  float2 inPosition;
  float3 inColor;
};

struct VSOutput {
  float4 pos : SV_POSITION;
  float3 color;
};


[shader("vertex")]
VSOutput vertMain(VSInput input) {
  VSOutput output;

  // Access UBO by index from push constants
  UniformBufferObject ubo = ubos[pc.objIdx];

  float4 worldPos = mul(ubo.model, float4(input.inPosition, 0.0, 1.0));
  float4 viewPos = mul(ubo.view, worldPos);
  // output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
  output.pos = mul(ubo.proj, viewPos);
  output.color = input.inColor;

  return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_Target {
  // return textures[NonUniformResourceIndex(pc.textureIdx)].Sample(vertIn.pos);
  return float4(vertIn.color, 1.0);
}
