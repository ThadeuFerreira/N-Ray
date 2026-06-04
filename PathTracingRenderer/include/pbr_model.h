#pragma once
#include <tri.h>
#include <vector>
#include <glm.hpp>
#include <cstdint>

struct Data;

struct PBRMaterial {
	glm::vec3 albedo;
	glm::vec3 specularCol;
	glm::vec3 emissionCol;
	glm::vec3 absorptionCol;
	glm::vec3 volumeCol;

	float IOR;
	float roughness;
	float emissionIntensity;
	float refraction;
	float absorption;
	float volume;
	float density;
	float metalness;
};

struct PTModel {
	std::vector<uint32_t> tris;

	bool doubleSided;
	uint32_t idx;
	uint32_t materialIdx;
	bool selected = false;

	PTModel(bool doubleSided, uint32_t idx, uint32_t materialIdx)
		: doubleSided(doubleSided), idx(idx), materialIdx(materialIdx) {

	}

	void updateTris(Data& data);
};
