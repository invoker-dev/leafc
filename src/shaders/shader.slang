struct VertexShaderInput {
  float3 pos;
  float3 normal;
  float3 UV;
};

// TODO: not in use for now
Sampler2D textures[];

struct ShaderData {
  float4x4 projection;
  float4x4 view;
  float4x4 model[3];
  float4 lightPos;
  uint32_t selected;
};

struct VertexShaderOutput {
  float4 pos : SV_POSITION
  float3 normal;
  float2 UV;
  float3 factor;
  float3 lightVec;
  float3 viewVec;
  uint32_t instanceIndex;
};

[shader("vertex")]
VertexShaderOutput main(
    VertexShaderInput input,
    uniform ShaderData *shaderData,
    uint instanceIndex : SV_VulkanInstanceID) {

  VertexShaderOutput output;
  float4x4 modelMatrix = shaderData->model[instanceIndex];
  output.Normal = mul((float3x3)mul(shaderData->view, modelMatrix), input.Normal);
  output.UV = input.UV;
  output.Pos = mul(shaderData->projection,
               mul(shaderData->view,
               mul(modelMatrix,
               float((input.Pos.xyz, 1.0))));
  output.factor = (shaderData->selected == instanceIndex ? 3.0f : 1.0f);
  output.instanceIndex = instanceIndex;
  // calculate view vocters required for lighting
  float4 fragPos = mul(mul(shadreData->View,modelMatrix),float4(input.Pos.xyz, 1.0));
  output.lightVec = shaderData->lightPos.xyz - fragPos.xyz;
  output.ViewVec = -fragPos.xyz;
  return output;
}

[shader("fragment")]
float4 main(VertexShaderOutput input) {
  // Phong lighting
  float3 N = normalize(input.Normal);
  float3 L = normalize(input.lightVec);
  float3 V = normalize(input.ViewVec);
  float3 R = reflect(-L, N);
  float3 diffuse = max(dot(N,L),0.0025);
  float3 specular = pow(max(dot(R,V),0.0),16.0) * 0.75;
  
  // TODO: Texture sampling

}


