// ============================================================================
//  shadow.slang  —  depth-only pass for shadow map generation
//
//  Improvements over the original:
//  ─────────────────────────────────────────────────────────────────────────
//  1. Normal-offset bias  — shifts the shadow-receiver position along its
//     world-space normal before projecting.  This eliminates self-shadowing
//     acne on sloped surfaces without the over-large constant bias that
//     causes Peter-Panning.
//
//  2. Slope-scale bias baked in via the offset magnitude.  The offset
//     scales with (1 - NdotL) so near-perpendicular surfaces (which have
//     the worst shadow acne) automatically get a larger push.
//
//  Usage: set pc.normalBias to a small world-space distance, e.g. 0.01–0.05.
//  You can leave the old per-fragment pc.shadowBias at a very small value
//  or even 0 now that the geometry-stage bias handles most of the work.
// ============================================================================

struct ShadowPush
{
    float4x4 lightViewProj;   // offset   0
    float4x4 modelMatrix;     // offset  64
    float3   lightDir;        // offset 128 — normalised sun direction in world space
    float    normalBias;      // offset 140 — world-space normal-offset distance
};

[[vk::push_constant]] ConstantBuffer<ShadowPush> pc;

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;     // needed for normal-offset bias
};

[shader("vertex")]
float4 vertexMain(VSInput input) : SV_Position
{
    // ── 1. World-space position ───────────────────────────────────────────
    float4 worldPos = mul(pc.modelMatrix, float4(input.position, 1.0));

    // ── 2. World-space normal (no need for full inverse-transpose here;
    //        shadow pass only needs approximate direction) ──────────────────
    float3 worldNormal = normalize(mul((float3x3)pc.modelMatrix, input.normal));

    // ── 3. Slope-scale normal offset ─────────────────────────────────────
    //  cosTheta = NdotL.  On surfaces facing the light (cosTheta ≈ 1) the
    //  offset is tiny.  On steep slopes (cosTheta ≈ 0) it grows, which is
    //  exactly where acne is worst.
    float  cosTheta = saturate(dot(worldNormal, normalize(pc.lightDir)));
    float  offset   = pc.normalBias * sqrt(1.0 - cosTheta * cosTheta) // tan(theta) proxy
                    / max(cosTheta, 0.001);
    offset          = min(offset, pc.normalBias * 8.0);              // clamp runaway on 90°

    worldPos.xyz += worldNormal * offset;

    // ── 4. Project into light clip space ─────────────────────────────────
    return mul(pc.lightViewProj, worldPos);
}
