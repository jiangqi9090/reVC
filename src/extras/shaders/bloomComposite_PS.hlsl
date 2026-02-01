sampler2D tex0 : register(s0);
sampler2D tex1 : register(s1);
float4 params : register(c10);  // x = bloomIntensity

float4 main(in float2 texcoord : TEXCOORD0) : COLOR0
{
	float4 scene = tex2D(tex0, texcoord.xy);
	float3 bloom = tex2D(tex1, texcoord.xy).rgb;

	float4 color;
	color.rgb = scene.rgb + bloom * params.x;
	color.a = 1.0;

	return color;
}
