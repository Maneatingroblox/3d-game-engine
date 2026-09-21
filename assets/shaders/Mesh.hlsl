// Main forward-lit mesh shader: combines dynamic lights (directional/point/
// spot with PCF shadow sampling for the primary light) with an optional
// baked lightmap sampled from UV1, plus a simple Cook-Torrance-ish PBR term.
//
// Matrix convention: mul(matrix, vector) everywhere - see Common.hlsli.
#include "Common.hlsli"

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float3 tangent  : TANGENT;
    float2 uv0      : TEXCOORD0;
    float2 uv1      : TEXCOORD1;
    float4 color    : COLOR0;
};

struct VSOutput {
    float4 clipPos      : SV_POSITION;
    float3 worldPos     : TEXCOORD0;
    float3 worldNormal  : TEXCOORD1;
    float3 worldTangent : TEXCOORD2;
    float2 uv0          : TEXCOORD3;
    float2 uv1          : TEXCOORD4;
    float4 color        : COLOR0;
    float4 lightClipPos : TEXCOORD5;
};

VSOutput VSMain(VSInput input) {
    VSOutput o;
    float4 worldPos4 = mul(gWorld, float4(input.position, 1.0));
    o.worldPos = worldPos4.xyz;
    o.clipPos = mul(gViewProj, worldPos4);
    o.worldNormal = mul((float3x3)gWorldInvTranspose, input.normal);
    o.worldTangent = mul((float3x3)gWorld, input.tangent);
    o.uv0 = input.uv0;
    o.uv1 = input.uv1;
    o.color = input.color;
    o.lightClipPos = mul(gLightViewProj, worldPos4);
    return o;
}

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (3.14159265 * denom * denom + 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Shadow map lookup. The depth buffer is Direct3D-convention [0,1] (the
// projection matrices are RH_ZO), the sampler compares LESS_EQUAL, and the
// depth texture is written by a depth-only pass, so "ref < stored" == lit.
float SampleShadow(float4 lightClipPos) {
    if (lightClipPos.w <= 0.0) return 1.0;
    float3 proj = lightClipPos.xyz / lightClipPos.w;
    float2 uv = proj.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) return 1.0;
    if (proj.z < 0.0 || proj.z > 1.0) return 1.0;

    float shadow = 0.0;
    float2 texel = 1.0 / 2048.0;
    [unroll]
    for (int x = -1; x <= 1; x++) {
        [unroll]
        for (int y = -1; y <= 1; y++) {
            shadow += gShadowMap.SampleCmpLevelZero(gShadowSampler, uv + float2(x, y) * texel, proj.z - 0.0015);
        }
    }
    return shadow / 9.0;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    float4 albedo = gAlbedoColor;
    if (gHasAlbedoMap) albedo *= gAlbedoMap.Sample(gLinearSampler, input.uv0);
    albedo *= input.color;

    float3 N = normalize(input.worldNormal);
    if (gHasNormalMap) {
        float3 T = normalize(input.worldTangent - N * dot(input.worldTangent, N));
        float3 B = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
        float3 nmap = gNormalMap.Sample(gLinearSampler, input.uv0).xyz * 2.0 - 1.0;
        N = normalize(mul(nmap, TBN));
    }

    float metallic = gMetallic;
    float roughness = max(gRoughness, 0.04);
    if (gHasMetallicRoughnessMap) {
        float4 mr = gMetallicRoughnessMap.Sample(gLinearSampler, input.uv0);
        metallic = mr.b;
        roughness = max(mr.g, 0.04);
    }

    float3 V = normalize(gCameraPos - input.worldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, metallic);

    float3 Lo = float3(0, 0, 0);
    for (int i = 0; i < gLightCount; i++) {
        GPULight light = gLights[i];
        float3 L;
        float attenuation = 1.0;
        if (light.type < 0.5) {
            L = normalize(-light.positionOrDir);
        } else {
            float3 toLight = light.positionOrDir - input.worldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-5);
            attenuation = saturate(1.0 - dist / max(light.range, 1e-3));
            attenuation *= attenuation;
            if (light.type > 1.5) {
                float cosAngle = dot(-L, normalize(light.spotDirection));
                float spot = saturate((cosAngle - light.outerCos) / max(light.innerCos - light.outerCos, 1e-4));
                attenuation *= spot * spot;
            }
        }

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        float3 H = normalize(V + L);
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(max(dot(N, V), 0.0), NdotL, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float3 kS = F;
        float3 kD = (1.0 - kS) * (1.0 - metallic);

        float3 numerator = NDF * G * F;
        float denom = 4.0 * max(dot(N, V), 0.0) * NdotL + 1e-4;
        float3 specular = numerator / denom;

        float shadowFactor = 1.0;
        if (i == 0 && light.castsShadow > 0.5 && gReceiveShadows) shadowFactor = SampleShadow(input.lightClipPos);

        float3 radiance = light.color * light.intensity * attenuation;
        Lo += (kD * albedo.rgb / 3.14159265 + specular) * radiance * NdotL * shadowFactor;
    }

    float3 ambient = gAmbientColor * gAmbientIntensity * albedo.rgb;
    if (gUseLightmap) {
        float3 lm = gLightmap.Sample(gLinearSampler, input.uv1).rgb;
        ambient = lm * albedo.rgb;
    }

    float3 emissive = gEmissiveColor * gEmissiveStrength;
    if (gHasEmissiveMap) emissive *= gEmissiveMap.Sample(gLinearSampler, input.uv0).rgb;

    float3 color = ambient + Lo + emissive;
    return float4(FWToDisplay(color), albedo.a);
}
