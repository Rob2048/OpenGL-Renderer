#ifndef _BRDFS_INC_
#define _BRDFS_INC_

struct vsMetalRough
{
    vec3 baseColor;
    float roughness;
    float metallic;
    vec3 f0;
};

float Attenuation(float Distance, float Radius)
{
    float A = pow(Distance / Radius, 4.0);
    float B = clamp(1.0 - A, 0.0, 1.0);
    float falloff = pow(B, 2.0) / (pow(Distance, 2.0) + 1.0);

    return falloff;
}

// Calculate the reflectance based on angle of incidence and reflection at zero incidence.
vec3 fresnelSchlick(float CosTheta, vec3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - CosTheta, 5.0f);
}

float distributionGGX(vec3 N, vec3 H, float Roughness)
{
    float a = Roughness * Roughness;
    float a2 = a * a;
    float ndh = max(dot(N, H), 0.0f);
    float ndh2 = ndh * ndh;

    float nom = a2;
    float denom = (ndh2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return nom / denom;
}

float geometrySchlickGGX(float Ndv, float Roughness)
{
    float r = Roughness + 1.0f;
    float k = (r * r) / 8.0f;

    float nom = Ndv;
    float denom = Ndv * (1.0f - k) + k;

    return nom / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float Roughness)
{
    float ndv = max(dot(N, V), 0.0f);
    float ndl = max(dot(N, L), 0.0f);
    float ggx2 = geometrySchlickGGX(ndv, Roughness);
    float ggx1 = geometrySchlickGGX(ndl, Roughness);

    return ggx1 * ggx2;
}

vec3 BlinnPhongBRDF()
{
    /*
    vec3 lightDir = normalize(lightToVert);
    vec3 reflectDir = reflect(-lightDir, norm);

    float specPower = 64.0f;
    float specGloss = 0.5f;
    float spec = pow(max(dot(viewDir, reflectDir), 0.0f), specPower);

    specColor += light.color.rgb * att * spec * specGloss;
    lightColor += clamp(dot(lightDir, norm), 0, 1) * light.color.rgb * att;
    */

    return vec3(0);
}

vec3 CookTorranceBRDF(vec3 N, vec3 V, vec3 L, vec3 F0, vec3 LightColor, vec3 BaseColor, float Metallic, float Roughness, float Attenuation)
{
    vec3 H = normalize(V + L);

    vec3 radiance = LightColor * Attenuation;

    float ndf = distributionGGX(N, H, Roughness);
    float g = geometrySmith(N, V, L, Roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0);

    vec3 nom = ndf * g * F; // NOTE: kS Not needed because it is here as F!
    float denom = 4 * max(dot(V, N), 0.0f) * max(dot(L, N), 0.0f) + 0.001f;
    vec3 brdf = nom / denom;

    // Calculate the ratio of spec & diffuse. (reflect vs refract)
    vec3 kS = F;
    vec3 kD = vec3(1.0f) - kS;
    kD *= 1.0f - Metallic;

    float ndl = max(dot(N, L), 0.0f);
    
    //Lo += (kD * pbrBaseColor / PI + brdf) * radiance * ndl;
    vec3 brdfOut = (kD * BaseColor / PI + brdf) * radiance * ndl;

    return brdfOut;
}

vec3 SphericalPoint(vec2 Xi, float Roughness)
{
    float a = Roughness * Roughness;

    // Compute distribution direction.
    float Phi = 2 * PI * Xi.x;
    //float CosTheta = (1.0 - Xi.y);
    //float SinTheta = sqrt(1.0 - CosTheta * CosTheta); 
    float CosTheta = sqrt((1 - Xi.y) / (1 + (a*a - 1) * Xi.y));     
    float SinTheta = sqrt(1 - CosTheta * CosTheta);

    // Convert to spherical distribution.
    vec3 H;
    H.x = SinTheta * cos(Phi);
    H.y = SinTheta * sin(Phi);
    H.z = CosTheta;

    return H;
}

vec3 SphericalPoint(vec2 Xi)
{
    float Phi = 2 * PI * Xi.x;
    float CosTheta = (1.0 - Xi.y);
    float SinTheta = sqrt(1.0 - CosTheta * CosTheta); 
    
    vec3 H;
    H.x = SinTheta * cos(Phi);
    H.y = SinTheta * sin(Phi);
    H.z = CosTheta;

    return H;
}

/*
float ComputeLod(uint NumSamples, vec3 N, vec3 H, float Roughness)
{
    //return 7;
    float dist = distributionGGX(N, H, Roughness);
    return 0.5 * (log2(2048.0 / 1024.0 / float(NumSamples)) - log2(dist));
    //return 0.5 * (log2(2048.0 / 1024.0 / float(NumSamples)) - log2(dist));
}
*/

float distortion(vec3 Wn)
{
  // Computes the inverse of the solid angle of the (differential) pixel in
  // the cube map pointed at by Wn
  float sinT = sqrt(1.0-Wn.y*Wn.y);
  return sinT;
}

float computeLOD(uint NumSamples, vec3 L, vec3 N, vec3 H, float Roughness)
{
    float mipOffset = 2;
    float maxLod = 11 - mipOffset;
    float p = distributionGGX(N, H, Roughness);    
    return max(0.0, (maxLod-1.5) - 0.5*(log(float(NumSamples)) + log( p * distortion(L) )) * INV_LOG2) + mipOffset;

    //float maxLod = 11;
    //float p = distributionGGX(N, H, Roughness);    
    //return max(0.0, (maxLod-1.5) - 0.5*(log(float(NumSamples)) + log( p * distortion(L) )) * INV_LOG2);
}

vec3 Radiance(vec3 N, vec3 V, float Roughness, vec3 F0, sampler2D EnvMap)
{
    float rough = Roughness;
    vec3 upVector = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangentX = normalize(cross(upVector, N));
    vec3 tangentY = cross(N, tangentX);

    // TODO: Maybe saturate?
    float ndv = abs(dot(N, V));

    vec3 fColor = vec3(0.0);
    const uint numSamples = 20;
    for (uint i = 0; i < numSamples; ++i)
    {
        vec2 Xi = Hammersley(i, numSamples);
        vec3 Li = SphericalPoint(Xi, rough);

        // Convert to world space.
        vec3 H = normalize(Li.x * tangentX + Li.y * tangentY + Li.z * N);
        vec3 L = normalize(-reflect(V, H));

        float ndl = abs(dot(N, L));
        float ndh = abs(dot(N, H));
        float vdh = abs(dot(V, H));
        //float lod = ComputeLod(numSamples, N, H, 1.0 - rough);
        float lod = computeLOD(numSamples, L, N, H, rough);

        vec3 F_ = fresnelSchlick(max(dot(H, V), 0.0f), F0);
        float G_ = geometrySmith(N, V, L, rough);
        vec3 LColor = (sampleEnvMap(EnvMap, L, lod).rgb);

        fColor += F_ * G_ * LColor * vdh / (ndh * ndv);
    }

    return fColor / float(numSamples);
}

#endif