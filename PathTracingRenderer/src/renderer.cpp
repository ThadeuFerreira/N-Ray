#include "renderer.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/rotate_vector.hpp>

namespace {
uint64_t splitMix64(uint64_t x) {
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

float randomSignedUnit(RenderRng& rng) {
	return rng.nextFloat01() * 2.0f - 1.0f;
}

glm::vec3 safeNormalize(const glm::vec3& value, const glm::vec3& fallback) {
	float lenSq = glm::dot(value, value);
	if (!std::isfinite(lenSq) || lenSq <= 0.00000001f) {
		return fallback;
	}

	return glm::normalize(value);
}
}

RenderRng::RenderRng(uint64_t seed) {
	state = splitMix64(seed);
}

uint32_t RenderRng::nextU32() {
	uint64_t oldState = state;
	state = oldState * 6364136223846793005ull + 1442695040888963407ull;
	uint32_t xorshifted = static_cast<uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
	uint32_t rot = static_cast<uint32_t>(oldState >> 59u);
	return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

float RenderRng::nextFloat01() {
	return static_cast<float>((nextU32() >> 8) * (1.0 / 16777216.0));
}

RenderRng makeRenderRng(uint32_t pixelIndex, uint32_t sampleIndex, uint32_t rayIndex) {
	uint64_t seed = splitMix64(uint64_t(pixelIndex) ^ 0xD1B54A32D192ED03ull) ^
		splitMix64(uint64_t(sampleIndex) ^ 0x9E3779B97F4A7C15ull) ^
		splitMix64(uint64_t(rayIndex) ^ 0xBF58476D1CE4E5B9ull);
	return RenderRng(seed);
}

bool PathTracer::RayIntersectsTriangle(PathRay& ray, const TriIntersect& tri, float& t, float& hitU, float& hitV) {
	const float EPSILON = 0.0000001f;

	glm::vec3 h = glm::cross(ray.dir, tri.eB);
	float det = glm::dot(tri.eA, h);

	if (tri.doubleSided) {
		if (det > -EPSILON && det < EPSILON) {
			return false;
		}
	}
	else {
		if (det < EPSILON) {
			return false;
		}
	}

	float invDet = 1.0f / det;

	glm::vec3 s = ray.src - tri.a;
	float u = invDet * glm::dot(s, h);

	if (u < 0.0f || u > 1.0f) {
		return false;
	}

	glm::vec3 q = glm::cross(s, tri.eA);
	float v = invDet * glm::dot(ray.dir, q);

	if (v < 0.0f || u + v > 1.0f) {
		return false;
	}

	t = invDet * glm::dot(tri.eB, q);

	if (t > EPSILON) {
		hitU = u;
		hitV = v;
		return true;
	}

	return false;
}

bool PathTracer::rayAABB(const PathRay& ray, const glm::vec3& boxMin, const glm::vec3& boxMax, float maxT) {
	// Branchless slab test using the precomputed reciprocal direction. min/max
	// fold the per-axis swap away and behave correctly for negative directions;
	// a degenerate (zero) direction yields ±inf and is still ordered by min/max.
	glm::vec3 t0 = (boxMin - ray.src) * ray.invDir;
	glm::vec3 t1 = (boxMax - ray.src) * ray.invDir;

	glm::vec3 tSmall = glm::min(t0, t1);
	glm::vec3 tBig = glm::max(t0, t1);

	float tmin = std::max(std::max(tSmall.x, tSmall.y), std::max(tSmall.z, 0.0f));
	float tmax = std::min(std::min(tBig.x, tBig.y), std::min(tBig.z, maxT));

	return tmax >= tmin;
}

void PathTracer::diffuseLighting(PathRay& ray, PathRayState& rayState, glm::vec3& normal, const std::vector<Tri>& tris, RenderRng& rng) {
	float u1 = rng.nextFloat01();
	float u2 = rng.nextFloat01();
	float r = sqrtf(u1);
	float phi = 2.0f * PI * u2;

	glm::vec3 localDir(
		r * cosf(phi),
		r * sinf(phi),
		sqrtf(1.0f - u1)
	);

	glm::vec3 up = fabsf(normal.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
	glm::vec3 tangent = safeNormalize(glm::cross(up, normal), glm::vec3(1, 0, 0));
	glm::vec3 bitangent = glm::cross(normal, tangent);

	glm::vec3 worldDir =
		localDir.x * tangent +
		localDir.y * bitangent +
		localDir.z * normal;

	worldDir = safeNormalize(worldDir, normal);

	if (glm::dot(worldDir, tris[rayState.triIdx].normal) < 0.0f) {
		worldDir = -worldDir;
	}

	ray.dir = worldDir;
}

glm::vec3 PathTracer::sampleGGX(const glm::vec3& normal, float roughness, float r1, float r2) {
	float a = roughness * roughness;

	float theta = atan(a * sqrt(r1) / sqrt(1.0f - r1));
	float phi = 2.0f * PI * r2;

	glm::vec3 h = glm::vec3(
		sin(theta) * cos(phi),
		sin(theta) * sin(phi),
		cos(theta)
	);

	glm::vec3 up = fabs(normal.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
	glm::vec3 tangent = safeNormalize(glm::cross(up, normal), glm::vec3(1, 0, 0));
	glm::vec3 bitangent = glm::cross(normal, tangent);

	return safeNormalize(tangent * h.x + bitangent * h.y + normal * h.z, normal);
}

bool PathTracer::specularLighting(PathRay& ray, PathRayState& rayState, glm::vec3& normal, const std::vector<Tri>& tris, const std::vector<PBRMaterial>& materials, RenderRng& rng) {
	const PBRMaterial& material = materials[tris[rayState.triIdx].materialIdx];

	float r1 = rng.nextFloat01();
	float r2 = rng.nextFloat01();
	glm::vec3 microfacetNormal = sampleGGX(normal, material.roughness, r1, r2);

	float r0Dielectric = ((airIOR - material.IOR) / (airIOR + material.IOR));
	r0Dielectric = r0Dielectric * r0Dielectric;

	float r0 = glm::mix(r0Dielectric, 1.0f, material.metalness);

	float cosThetaI = glm::clamp(-glm::dot(ray.dir, microfacetNormal), 0.0f, 1.0f);
	float rTheta = r0 + (1.0f - r0) * std::pow(1.0f - cosThetaI, 5.0f);

	if (rng.nextFloat01() > rTheta) {
		return false;
	}

	glm::vec3 reflectedDir = glm::reflect(ray.dir, microfacetNormal);

	if (glm::dot(reflectedDir, normal) <= 0.0f ||
		glm::dot(reflectedDir, tris[rayState.triIdx].normal) <= 0.0f) {
		return false;
	}

	ray.dir = reflectedDir;
	return true;
}

void PathTracer::refractionLighting(PathRay& ray, PathRayState& rayState, glm::vec3 normal, const std::vector<Tri>& tris, const std::vector<PBRMaterial>& materials) {
	const PBRMaterial& material = materials[tris[rayState.triIdx].materialIdx];
	float n1 = airIOR;
	float n2 = material.IOR;

	float cosi = glm::dot(ray.dir, normal);

	bool isExiting = cosi > 0.0f;

	if (isExiting) {
		std::swap(n1, n2);

		normal = -normal;

		float dist = glm::distance(rayState.hitPos, ray.src);

		float absorptionFactor = dist * material.absorption;
		glm::vec3 absorptionScale = glm::exp(-material.absorptionCol * absorptionFactor);

		rayState.throughput *= absorptionScale;
	}

	if (n2 <= 0.0001f) {
		ray.dir = glm::reflect(ray.dir, normal);
		ray.src = rayState.hitPos + normal * 0.001f;
		rayState.isRefraction = true;
		return;
	}

	float eta = n1 / n2;

	float k = 1.0f - eta * eta * (1.0f - cosi * cosi);

	if (k <= 0.00000001f) {
		ray.dir = glm::reflect(ray.dir, normal);
		ray.src = rayState.hitPos + normal * 0.001f;
		rayState.isRefraction = true;
		return;
	}

	ray.dir = safeNormalize(glm::refract(ray.dir, normal, eta), glm::reflect(ray.dir, normal));

	if (isExiting) {
		ray.src = rayState.hitPos - normal * 0.001f;
		rayState.isRefraction = false;
	}
	else {
		ray.src = rayState.hitPos - normal * 0.001f;
		rayState.isRefraction = true;
	}
}

uint32_t PathTracer::flattenBVH(uint32_t buildNodeIdx, const std::vector<BVH>& buildNodes, std::vector<CompactBVH>& flatNodes) {

	// Capture everything we need before recursing: child push_backs reallocate
	// flatNodes, so we re-index by `myFlatIndex` rather than holding a reference.
	const BVH& buildNode = buildNodes[buildNodeIdx];
	const glm::vec3 nodeMin = buildNode.min;
	const glm::vec3 nodeMax = buildNode.max;
	const uint32_t child0 = buildNode.children[0];
	const uint32_t child1 = buildNode.children[1];
	const uint32_t startIndex = buildNode.startIndex;
	const uint32_t endIndex = buildNode.endIndex;
	const uint8_t axis = buildNode.splitAxis;

	uint32_t myFlatIndex = static_cast<uint32_t>(flatNodes.size());

	CompactBVH compactNode{};
	compactNode.min = nodeMin;
	compactNode.max = nodeMax;
	flatNodes.push_back(compactNode);

	if (child0 == UINT32_MAX && child1 == UINT32_MAX) {
		flatNodes[myFlatIndex].triCount = static_cast<uint16_t>(endIndex - startIndex + 1);
		flatNodes[myFlatIndex].startIndex = startIndex;
	}
	else {
		flatNodes[myFlatIndex].triCount = 0;
		flatNodes[myFlatIndex].axis = axis;

		// First child sits immediately after this node; only the second is linked.
		flattenBVH(child0, buildNodes, flatNodes);
		uint32_t secondChild = flattenBVH(child1, buildNodes, flatNodes);
		flatNodes[myFlatIndex].secondChild = secondChild;
	}

	return myFlatIndex;
}

void PathTracer::traverseFlatBVH(PathRay& ray, PathRayState& rayState, float& closestT, const std::vector<TriIntersect>& triIsect, const std::vector<CompactBVH>& flatBVH) {

	if (flatBVH.empty()) return;

	// Visit the child on the same side as the ray direction first: closestT
	// shrinks sooner, so more of the far sub-tree fails the slab test outright.
	const bool dirIsNeg[3] = {
		ray.invDir.x < 0.0f,
		ray.invDir.y < 0.0f,
		ray.invDir.z < 0.0f
	};

	uint32_t stack[64];
	int stackPtr = 0;
	uint32_t current = 0;

	while (true) {
		const CompactBVH& node = flatBVH[current];

		if (rayAABB(ray, node.min, node.max, closestT)) {
			if (node.triCount > 0) {
				const uint32_t start = node.startIndex;
				for (uint32_t i = 0; i < node.triCount; ++i) {
					float t;
					float hitU = 0.0f;
					float hitV = 0.0f;
					const TriIntersect& tri = triIsect[start + i];

					if (RayIntersectsTriangle(ray, tri, t, hitU, hitV) && t < closestT) {
						closestT = t;
						rayState.hit = true;
						rayState.hitPos = ray.src + ray.dir * t;
						rayState.triIdx = tri.idx;
						rayState.hitU = hitU;
						rayState.hitV = hitV;
					}
				}

				if (stackPtr == 0) break;
				current = stack[--stackPtr];
			}
			else {
				if (dirIsNeg[node.axis]) {
					stack[stackPtr++] = current + 1;
					current = node.secondChild;
				}
				else {
					stack[stackPtr++] = node.secondChild;
					current = current + 1;
				}
			}
		}
		else {
			if (stackPtr == 0) break;
			current = stack[--stackPtr];
		}
	}
}

glm::vec3 PathTracer::InterpolateNormal(PathRayState& rayState, const std::vector<Tri>& tris) {
	float v = rayState.hitU;
	float w = rayState.hitV;
	float u = 1.0f - v - w;

	glm::vec3 interpolatedNormal =
		u * tris[rayState.triIdx].aN +
		v * tris[rayState.triIdx].bN +
		w * tris[rayState.triIdx].cN;

	return safeNormalize(interpolatedNormal, tris[rayState.triIdx].normal);
}

//void PathTracer::directLight(PathRay& ray, glm::vec3 normal, std::vector<Tri>& tris, Params& params) {
//
//	static thread_local std::mt19937 rng(std::random_device{}());
//	thread_local std::uniform_int_distribution<size_t> idx(0, params.emissiveAmount - 1);
//	thread_local std::uniform_real_distribution<float> uv(0.0f, 1.0f);
//
//	size_t emIdx = idx(rng);
//
//	float u = uv(rng);
//	float v = uv(rng);
//
//	if (u + v > 1.0f) {
//		u = 1.0f - u;
//		v = 1.0f - v;
//	}
//
//	glm::vec3 sampleP = u * tris[emIdx].a + v * tris[emIdx].b + (1.0f - u - v) * tris[emIdx].c;
//	glm::vec3 sampleN = tris[emIdx].normal;
//
//	glm::vec3 lVec = sampleP - ray.hitPos;
//	float dSq = glm::dot(lVec, lVec);
//	float dist = sqrt(dSq);
//	glm::vec3 dlDir = lVec / dist;
//
//	PathRay dlRay({ ray.hitPos + tris[ray.triIdx].normal * 0.001f, dlDir });
//
//	float closestT = FLT_MAX;
//	traverseFlatBVH(dlRay, closestT, tris, globalCompactBVH);
//
//	if (closestT < dist - 0.001f) {
//		return;
//	}
//
//	glm::vec3 ab = tris[emIdx].a - tris[emIdx].b;
//	glm::vec3 ac = tris[emIdx].a - tris[emIdx].c;
//
//	float area = 0.5f * glm::length(glm::cross(ab, ac));
//
//	float pdf = 1.0f / (float(params.emissiveAmount) * area);
//
//	glm::vec3 dlDirT = glm::normalize(sampleP - ray.hitPos);
//	glm::vec3 diff = sampleP - ray.hitPos;
//	float distSq = glm::dot(diff, diff);
//
//	float cosSurface = glm::max(glm::dot(normal, dlDirT), 0.0f);
//	float cosLight = glm::max(glm::dot(sampleN, -dlDirT), 0.0f);
//
//	float G = (cosSurface * cosLight) / distSq;
//
//	glm::vec3 emission = tris[emIdx].emissionCol * tris[emIdx].emissionIntensity;
//
//	ray.col += ray.throughput * emission * G / (pdf * PI);
//}

glm::vec3 sky(PathRay& ray, Params& params) {

	glm::vec3 skyCol(params.skyIntensity);

	if (params.enableSky) {
		glm::vec3 worldUp = { 0.0f, 0.0f, 1.0f };

		glm::vec3 skyTop = { 0.263f, 0.553f, 0.769f };
		glm::vec3 skyBase = { 0.89f, 0.824f, 0.698f };

		float upAmount = glm::pow(glm::max(glm::dot(ray.dir, worldUp), 0.0f), 0.5f);

		skyCol = glm::mix(skyBase, skyTop, upAmount) * params.skyIntensity;
	}

	if (params.enableSun) {
		float sunDot = glm::clamp(glm::dot(ray.dir, params.sunDir), -1.0f, 1.0f);
		float sunAngle = glm::acos(sunDot);

		if (glm::degrees(sunAngle) < params.sunAngle) {
			skyCol = params.sunColor * params.sunIntensity;
		}
	}

	return skyCol;
}

glm::vec3 hdriLogic(PathRay& ray, Params& params, const RenderEnvironment& environment) {

	// Fall back to the procedural sky if the HDRI failed to load (missing file, or
	// raylib built without SUPPORT_FILEFORMAT_HDR). Otherwise the zero dimensions
	// below produce a negative index into a null buffer and segfault.
	if (!environment.isValid()) {
		return sky(ray, params);
	}

	float phi = glm::atan(ray.dir.y, ray.dir.x);

	float theta = glm::asin(glm::clamp(ray.dir.z, -1.0f, 1.0f));

	float u = (phi + PI) / (2.0f * PI);
	float v = 1.0f - (theta + PI * 0.5f) / PI;

	int x = glm::clamp((int)(u * environment.width), 0, environment.width - 1);
	int y = glm::clamp((int)(v * environment.height), 0, environment.height - 1);
	int pIdx = (y * environment.width + x) * environment.channels;

	glm::vec3 pCol = {
		environment.pixels[pIdx + 0],
		environment.pixels[pIdx + 1],
		environment.pixels[pIdx + 2]
	};

	return pCol * params.skyIntensity;
}

//void PathTracer::sampleSun(PathRay& ray, std::vector<Tri>& tris, Params& params, bool& isShadow) {
//
//	PathRay sunRay;
//
//	sunRay.src = ray.hitPos + tris[ray.triIdx].normal * 0.001f;
//
//	static thread_local std::mt19937 rng(std::random_device{}());
//	static thread_local std::uniform_real_distribution<float> dist(0.0f, 1.0f);
//
//	glm::vec3 sunDir = params.sunDir;
//	sunDir = glm::normalize(sunDir);
//
//	float phi = 2.0f * PI * dist(rng);
//
//	float halfAngle = glm::radians(params.sunAngle * 0.5f);
//	float cosMax = std::cos(halfAngle);
//	float cosTheta = 1.0f - dist(rng) * (1.0f - cosMax);
//	float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
//
//	glm::vec3 worldUp = { 0.0f, 0.0f, 1.0f };
//	worldUp = glm::normalize(worldUp);
//
//	if (sunDir == worldUp) {
//		worldUp = { 0.0f, 0.5f, 1.0f };
//		worldUp = glm::normalize(worldUp);
//	}
//
//	glm::vec3 tangent = glm::normalize(glm::cross(sunDir, worldUp));
//	glm::vec3 bitangent = glm::normalize(glm::cross(sunDir, tangent));
//
//	sunRay.dir = glm::normalize(
//		sinTheta * std::cos(phi) * tangent +
//		sinTheta * std::sin(phi) * bitangent +
//		cosTheta * sunDir
//	);
//	sunRay.invDir = 1.0f / sunRay.dir;
//
//	float closestTSun = FLT_MAX;
//	sunRay.hit = false;
//	sunRay.triIdx = UINT32_MAX;
//
//	traverseFlatBVH(sunRay, closestTSun, tris, globalCompactBVH);
//
//	if (!sunRay.hit) {
//		ray.col += ray.throughput * params.skyIntensity * sky(sunRay, params) / params.sunIntensity;
//	}
//	else {
//		isShadow = true;
//	}
//
//	// REPLACE THE SKY LOGIC IN rayLogic(); WITH THIS WHEN USING SUN SAMPLING
//
//	//if (!ray.hit) {
//		//	ray.active = false;
//
//		//	if (params.environmentLight) {
//
//
//		//		ray.col += ray.throughput * params.environmentIntensity * sky(ray, params);
//		//		/*if (!isShadow) {
//		//			float sunAngle = glm::acos(glm::dot(ray.dir, params.sunDir) / (glm::length(ray.dir) * glm::length(params.sunDir)));
//		//			if (glm::degrees(sunAngle) > params.sunAngle) {
//		//				ray.col += ray.throughput * params.environmentIntensity * sky(ray, params);
//		//			}
//		//		}
//		//		else {
//		//			ray.col += ray.throughput * params.environmentIntensity * sky(ray, params);
//		//		}*/
//		//	}
//		//	/*else if (params.environmentLight && bounce == 0) {
//		//		ray.col += ray.throughput * params.environmentIntensity * sky(ray, params);
//		//	}*/
//
//		//	break;
//		//}
//}

void PathTracer::rayLogic(PathRay& ray, PathRayState& rayState, const std::vector<Tri>& tris, const std::vector<TriIntersect>& triIsect, const std::vector<PBRMaterial>& materials, const std::vector<CompactBVH>& flatBVH, Params& params, const RenderEnvironment& environment, RenderRng& rng, std::vector<DebugRay>* debugOut) {
	for (int bounce = 0; bounce <= params.maxBounces; bounce++) {
		if (!rayState.active) {
			break;
		}

		float closestT = FLT_MAX;

		rayState.hit = false;
		rayState.triIdx = UINT32_MAX;

		traverseFlatBVH(ray, rayState, closestT, triIsect, flatBVH);

		if (debugOut) {
			float drawLength = closestT;
			if (closestT == FLT_MAX) {
				drawLength = 1000.0f;
			}

			debugOut->push_back({ ray.src, ray.dir, rayState.throughput, drawLength });
		}

		if (rayState.isVolume && rayState.triIdx != UINT32_MAX) {
			const PBRMaterial& material = materials[tris[rayState.triIdx].materialIdx];
			float scatterDist = FLT_MAX;
			if (material.density > 0.000001f) {
				float randomVal = std::max(rng.nextFloat01(), 0.0001f);
				scatterDist = -std::log(randomVal) / material.density;
			}

			if (scatterDist < closestT) {

				rayState.hitPos = ray.src + ray.dir * scatterDist;
				ray.src = rayState.hitPos;

				glm::vec3 randDir;
				do {
					randDir = glm::vec3(randomSignedUnit(rng), randomSignedUnit(rng), randomSignedUnit(rng));
				} while (glm::length(randDir) > 1.0f || glm::length(randDir) < 0.001f);

				ray.dir = safeNormalize(randDir, ray.dir);
				ray.invDir = 1.0f / ray.dir;

				rayState.throughput *= material.volumeCol;

				continue;
			}
		}

		if (rayState.triIdx != UINT32_MAX && rayState.active) {
			const PBRMaterial& material = materials[tris[rayState.triIdx].materialIdx];

			glm::vec3 interpolatedNormal = InterpolateNormal(rayState, tris);

			float cosTheta = glm::dot(ray.dir, interpolatedNormal);
			bool isInside = cosTheta > 0.0f;

			glm::vec3 orientedNormal = isInside ? -interpolatedNormal : interpolatedNormal;

			if (!rayState.isRefraction) {
				ray.src = rayState.hitPos + tris[rayState.triIdx].normal * 0.001f;
			}

			float emissionVal = (material.emissionCol.x + material.emissionCol.y + material.emissionCol.z) / 3.0f;
			bool isEmissive = material.emissionIntensity > 0.0f && emissionVal > 0.0f;

			if (isEmissive) {
				rayState.col += rayState.throughput * material.emissionCol * material.emissionIntensity;
			}

			bool isVolumeMaterial = rng.nextFloat01() < material.volume;

			if (isVolumeMaterial) {
				ray.src = rayState.hitPos - orientedNormal * 0.001f;

				if (!isInside) {
					rayState.isVolume = true;
				}
				else {
					rayState.isVolume = false;
				}
			}
			else {

				bool isSpecular = false;

				if (!rayState.isRefraction) {
					isSpecular = specularLighting(ray, rayState, interpolatedNormal, tris, materials, rng);
				}

				if (!isSpecular) {
					if (rng.nextFloat01() < material.refraction) {
						refractionLighting(ray, rayState, interpolatedNormal, tris, materials);
					}
					else {
						rayState.throughput *= material.albedo;

						diffuseLighting(ray, rayState, interpolatedNormal, tris, rng);
					}
				}
				else {
					rayState.throughput *= material.specularCol;
				}
			}
		}

		if (!rayState.hit && !rayState.isVolume) {
			rayState.active = false;

			//rayState.col += rayState.throughput * sky(ray, params);
			rayState.col += rayState.throughput * hdriLogic(ray, params, environment);

			break;
		}

		// Russian roulette: once a few bounces deep, kill the path with a
		// probability tied to its remaining energy and reweight the survivors.
		// This is unbiased (E[contribution] unchanged) but cuts average depth.
		if (params.russianRoulette && bounce >= params.rrMinBounces && rayState.active) {
			float pSurvive = glm::clamp(
				glm::max(rayState.throughput.x, glm::max(rayState.throughput.y, rayState.throughput.z)),
				0.05f, 1.0f);

			if (rng.nextFloat01() > pSurvive) {
				rayState.active = false;
				break;
			}

			rayState.throughput /= pSurvive;
		}

		ray.invDir = 1.0f / ray.dir;
	}
}

void PathTracer::generatePixelRay(uint32_t pixelIndex, PathRay& ray, PathRayState& rayState, const PTCam& myCam, const Screen& screen, const Params& params, RenderRng& rng) {
	int x = static_cast<int>(pixelIndex % static_cast<uint32_t>(screen.resX));
	int y = static_cast<int>(pixelIndex / static_cast<uint32_t>(screen.resX));

	float jitterX = rng.nextFloat01() - 0.5f;
	float jitterY = rng.nextFloat01() - 0.5f;

	float angle = rng.nextFloat01() * 2.0f * PI;
	float radius = myCam.aperture * sqrtf(rng.nextFloat01());
	glm::vec2 diskSample = { glm::cos(angle) * radius, glm::sin(angle) * radius };

	float srcOffsetX = (static_cast<float>(x) + 0.5f + jitterX * params.blur) / static_cast<float>(screen.resX) - 0.5f;
	float srcOffsetY = (static_cast<float>(y) + 0.5f + jitterY * params.blur) / static_cast<float>(screen.resY) - 0.5f;

	glm::vec3 src = myCam.camPos;

	src -= myCam.right * (srcOffsetX) * (myCam.sensorSize / 1000.0f);
	src += myCam.up * (srcOffsetY) * ((myCam.sensorSize / 1000.0f) / screen.ratio);

	glm::vec3 dir = safeNormalize(myCam.focalPoint - src, myCam.camNormal);

	glm::vec3 focusPoint = myCam.camPos + (dir * (myCam.focusDist / glm::dot(dir, myCam.camNormal)));

	src -= myCam.right * (diskSample.x) * (myCam.sensorSize / 1000.0f);
	src += myCam.up * (diskSample.y) * ((myCam.sensorSize / 1000.0f) / screen.ratio);

	dir = safeNormalize(focusPoint - src, myCam.camNormal);

	ray.src = src;
	ray.dir = dir;
	ray.invDir = 1.0f / dir;
	rayState.hitPos = src;
	rayState.col = glm::vec3(0.0f);
	rayState.throughput = glm::vec3(1.0f * myCam.ISO);
	rayState.length = FLT_MAX;
	rayState.hitU = 0.0f;
	rayState.hitV = 0.0f;
	rayState.triIdx = UINT32_MAX;
	rayState.hit = false;
	rayState.active = true;
	rayState.isRefraction = false;
	rayState.isVolume = false;
}
