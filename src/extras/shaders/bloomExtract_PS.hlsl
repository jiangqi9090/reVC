sampler2D tex : register(s0);
float4 params : register(c10);  // x = threshold

float4 main(in float2 texcoord : TEXCOORD0) : COLOR0
{
	float4 color = tex2D(tex, texcoord.xy);
	float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));

	if(brightness > params.x)
		return float4(color.rgb, 1.0);
	else
		return float4(0.0, 0.0, 0.0, 1.0);
}
