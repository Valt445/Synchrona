#version 460

#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in  vec3 inDirection;
layout(location = 0) out vec4 outColor;

layout(scalar, push_constant) uniform SkyPC {
    vec3  sunDirection;
    float time;
    vec2  resolution;
    float cloudCoverage;
    float cloudSpeed;
} pc;

const float cloudscale = 1.1;
const float speed      = 0.03;
const float clouddark  = 0.5;
const float cloudlight = 0.3;
const float cloudcover = 0.2;
const float cloudalpha = 8.0;
const float skytint    = 0.5;
const vec3  skycolour1 = vec3(0.2, 0.4, 0.6);
const vec3  skycolour2 = vec3(0.4, 0.7, 1.0);
const mat2  m          = mat2(1.6, 1.2, -1.2, 1.6);

vec2 hash(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise(in vec2 p) {
    const float K1 = 0.366025404;
    const float K2 = 0.211324865;
    vec2 i = floor(p + (p.x + p.y) * K1);
    vec2 a = p - i + (i.x + i.y) * K2;
    vec2 o = (a.x > a.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 b = a - o + K2;
    vec2 c = a - 1.0 + 2.0 * K2;
    vec3 h = max(0.5 - vec3(dot(a,a), dot(b,b), dot(c,c)), 0.0);
    vec3 n = h*h*h*h * vec3(dot(a,hash(i+0.0)), dot(b,hash(i+o)), dot(c,hash(i+1.0)));
    return dot(n, vec3(70.0));
}

float fbm(vec2 n) {
    float total = 0.0, amplitude = 0.1;
    for (int i = 0; i < 7; i++) {
        total += noise(n) * amplitude;
        n = m * n;
        amplitude *= 0.4;
    }
    return total;
}

void main()
{
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 p  = fragCoord / pc.resolution;
    vec2 uv = p * vec2(pc.resolution.x / pc.resolution.y, 1.0);

    float time = pc.time * speed;
    float q = fbm(uv * cloudscale * 0.5);

    float r = 0.0;
    vec2 uv2 = uv * cloudscale - q + time;
    float weight = 0.8;
    for (int i = 0; i < 8; i++) {
        r += abs(weight * noise(uv2));
        uv2 = m * uv2 + time;
        weight *= 0.7;
    }

    float f = 0.0;
    uv2 = p * vec2(pc.resolution.x / pc.resolution.y, 1.0) * cloudscale - q + time;
    weight = 0.7;
    for (int i = 0; i < 8; i++) {
        f += weight * noise(uv2);
        uv2 = m * uv2 + time;
        weight *= 0.6;
    }
    f *= r + f;

    float c = 0.0;
    float t2 = pc.time * speed * 2.0;
    uv2 = p * vec2(pc.resolution.x / pc.resolution.y, 1.0) * cloudscale * 2.0 - q + t2;
    weight = 0.4;
    for (int i = 0; i < 7; i++) {
        c += weight * noise(uv2);
        uv2 = m * uv2 + t2;
        weight *= 0.6;
    }

    float c1 = 0.0;
    float t3 = pc.time * speed * 3.0;
    uv2 = p * vec2(pc.resolution.x / pc.resolution.y, 1.0) * cloudscale * 3.0 - q + t3;
    weight = 0.4;
    for (int i = 0; i < 7; i++) {
        c1 += abs(weight * noise(uv2));
        uv2 = m * uv2 + t3;
        weight *= 0.6;
    }
    c += c1;
    vec3 skycolour = mix(skycolour2, skycolour1, p.y);
    vec3 cloudcolour = vec3(1.1, 1.1, 0.9) * clamp(clouddark + cloudlight * c, 0.0, 1.0);
    f = cloudcover + cloudalpha * f * r;

// Sun influence
    vec3 sun = normalize(pc.sunDirection);
    float sunHeight = clamp(sun.y, 0.0, 1.0);

// Shift sky colour at sunset/sunrise
    vec3 sunsetColour = vec3(0.8, 0.4, 0.1);
    skycolour = mix(mix(sunsetColour, skycolour2, sunHeight), skycolour1, p.y);

// Sun disc
    vec2 sunScreenPos = vec2(
    sun.x * 0.5 + 0.5,
    -sun.y * 0.5 + 0.5
    );
    float sunDist = length(p - sunScreenPos);
    float sunDisc = smoothstep(0.05, 0.01, sunDist);
    vec3 sunColour = vec3(1.0, 0.9, 0.6) * sunDisc * 3.0;

    vec3 result = mix(skycolour, clamp(skytint * skycolour + cloudcolour, 0.0, 1.0), clamp(f + c, 0.0, 1.0));
    result += sunColour;

    outColor = vec4(result, 1.0);
    
}
