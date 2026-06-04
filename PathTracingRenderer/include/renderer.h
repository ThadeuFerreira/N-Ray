#pragma once
#include <glm/glm.hpp>
#include <globalParams.h>
#include <camera.h>
#include <bvh.h>
#include <screenStartup.h>
#include <render_types.h>
#include <cstdint>
#include <iostream>


struct PathRay {
	glm::vec3 src;
	glm::vec3 dir;
	glm::vec3 invDir;
};

struct PathRayState {
	glm::vec3 hitPos;
	glm::vec3 col;
	glm::vec3 throughput;
	float length;
	float hitU = 0.0f;
	float hitV = 0.0f;
	uint32_t triIdx;
	bool hit = false;
	bool active = true;
	bool isRefraction = false;
	bool isVolume = false;
};

struct DebugRay {
	glm::vec3 src;
	glm::vec3 dir;
	glm::vec3 col;
	float length;
	float progress = 0.0f;
};

struct Params;

struct RenderRng {
	uint64_t state = 0;

	explicit RenderRng(uint64_t seed = 1);
	uint32_t nextU32();
	float nextFloat01();
};

RenderRng makeRenderRng(uint32_t pixelIndex, uint32_t sampleIndex, uint32_t rayIndex);

struct PathTracer {

	bool RayIntersectsTriangle(PathRay& ray, const Tri& tri, float& t, float& hitU, float& hitV);

	bool rayAABB(const PathRay& ray, const glm::vec3& boxMin, const glm::vec3& boxMax, float maxT);

	void diffuseLighting(PathRay& ray, PathRayState& rayState, glm::vec3& normal, const std::vector<Tri>& tris, RenderRng& rng);

	const float airIOR = 1.0f;

	glm::vec3 sampleGGX(const glm::vec3& normal, float roughness, float r1, float r2);

	bool specularLighting(PathRay& ray, PathRayState& rayState, glm::vec3& normal, const std::vector<Tri>& tris, const std::vector<PBRMaterial>& materials, RenderRng& rng);

	void refractionLighting(PathRay& ray, PathRayState& rayState, glm::vec3 normal, const std::vector<Tri>& tris, const std::vector<PBRMaterial>& materials);

	void flattenBVH(uint32_t buildNodeIdx, const std::vector<BVH>& buildNodes, std::vector<CompactBVH>& flatNodes);

	void traverseFlatBVH(PathRay& ray, PathRayState& rayState, float& closestT, const std::vector<Tri>& tris, const std::vector<CompactBVH>& flatBVH);

	void directLight(PathRay& ray, glm::vec3 normal, std::vector<Tri>& tris, Params& params);

	glm::vec3 InterpolateNormal(PathRayState& rayState, const std::vector<Tri>& tris);

	void sampleSun(PathRay& ray, std::vector<Tri>& tris, Params& params, bool& isShadow); // CURRENTLY UNUSED

	std::vector<DebugRay> rayLogic(PathRay& ray, PathRayState& rayState, const std::vector<Tri>& tris, const std::vector<PBRMaterial>& materials, const std::vector<CompactBVH>& flatBVH, Params& params, const RenderEnvironment& environment, RenderRng& rng, bool debug = false);

	void generatePixelRay(uint32_t pixelIndex, PathRay& ray, PathRayState& rayState, const PTCam& myCam, const Screen& screen, const Params& params, RenderRng& rng);

	glm::vec3 contrastSCurve(glm::vec3 x, float c) {

		auto curve = [&](float v) -> float {
			if (v < 0.5f)
				return 0.5f * std::pow(2.0f * v, 1.0f + c);
			else
				return 1.0f - 0.5f * std::pow(2.0f * (1.0f - v), 1.0f + c);
			};
		return glm::clamp(glm::vec3(curve(x.r), curve(x.g), curve(x.b)), 0.0f, 1.0f);
	}

	void colorManagement(float c, glm::vec3& col) {
		col = contrastSCurve(col, c);
		col = glm::pow(col, glm::vec3(1.0f / 2.2f));
	}

};
