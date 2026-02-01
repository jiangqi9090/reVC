sampler2D tex : register(s0);
float4 params : register(c10);  // xy = texelSize, z = horizontal

float4 main(in float2 texcoord : TEXCOORD0) : COLOR0
{
	// 9-tap Gaussian weights
	float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };

	float3 result = tex2D(tex, texcoord.xy).rgb * weights[0];

	float2 offset;
	if(params.z > 0.5)
		offset = float2(params.x, 0.0);
	else
		offset = float2(0.0, params.y);

	for(int i = 1; i < 5; i++) {
		result += tex2D(tex, texcoord.xy + offset * float(i)).rgb * weights[i];
		result += tex2D(tex, texcoord.xy - offset * float(i)).rgb * weights[i];
	}

	return float4(result, 1.0);
}
