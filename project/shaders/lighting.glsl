#define PI 3.14159265359

float D_GGX(float NoH, float a) 
{
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f);
}

vec3 F_Schlick(float u, vec3 f0) {
    return f0 + (vec3(1.0) - f0) * pow(1.0 - u, 5.0);
}

float G_SmithGGXCorrelated(float NoV, float NoL, float a) 
{
    float GGXV = NoL * (NoV * (1.0 - a) + a);
    float GGXL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL);
}

float Fd_Lambert() {
    return 1.0 / PI;
}

vec3 CookTorrance_BRDF(vec3 matColour, vec3 lightColour, vec3 n, vec3 v, vec3 l, float perceptualRoughness , float metallic) 
{
    vec3 h = normalize(v + l);

    float NoV = abs(dot(n, v)) + 1e-5;
    float NoL = clamp(dot(n, l), 0.0, 1.0);
    float NoH = clamp(dot(n, h), 0.0, 1.0);
    float VoH = clamp(dot(v, h), 0.0, 1.0);

    float roughness = perceptualRoughness  * perceptualRoughness ;

	vec3 F0 = vec3(0.04);  // Dielectric default
    F0 = mix(F0, matColour.rgb, metallic); // Lerp to metallic behavior

    float D = D_GGX(NoH, roughness);
    float G = G_SmithGGXCorrelated(NoV, NoL, roughness);
    vec3  F = F_Schlick(VoH, F0);

    // specular BRDF
	vec3 specular = (D * G) * F;

	// === ENERGY CONSERVATION ===
	// Fresnel term represents specular reflection ratio
	vec3 kS = F;                          // Specular contribution
	vec3 kD = vec3(1.0, 1.0, 1.0) - kS; // Diffuse contribution (energy conservation)
	kD *= 1.0 - metallic;                   // Metals have no diffuse reflection

    // diffuse BRDF
    vec3 Fd = kD * matColour * Fd_Lambert();

	return (Fd + specular) * lightColour * NoL;
}

vec3 Phong_BRDF(vec3 matColour, vec3 lightColour, vec3 n, vec3 v, vec3 l, float exp)
{
    vec3 h = normalize(v + l);

	// lambertian BRDF
	float lightValue = dot(n, l);
	vec3 diff = lightValue * matColour * lightColour;

    float spec = pow(max(dot(n, h), 0.0), exp);
    vec3 specular = lightColour * spec;

    return diff + specular;
}