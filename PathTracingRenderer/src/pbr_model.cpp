#include "pbr_model.h"
#include "globalParams.h"

void PTModel::updateTris(Data& data) {
	if (materialIdx < data.materials.size()) {
		PBRMaterial& material = data.materials[materialIdx];
		material.albedo = albedo;
		material.specularCol = specularCol;
		material.emissionCol = emissionCol;
		material.absorptionCol = absorptionCol;
		material.volumeCol = volumeCol;
		material.IOR = IOR;
		material.roughness = roughness;
		material.emissionIntensity = emissionIntensity;
		material.refraction = refraction;
		material.absorption = absorption;
		material.volume = volume;
		material.density = density;
		material.metalness = metalness;
	}

	for (uint32_t triIdx : tris) {
		if (triIdx < data.tris.size()) {
			data.tris[triIdx].doubleSided = doubleSided;
		}
	}
}
