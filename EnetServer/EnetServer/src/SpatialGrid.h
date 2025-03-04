#pragma once
#include <cstdint>
#include <set>
#include <unordered_map>
#include <mutex>
#include "Structs.h"

class SpatialGrid
{
public:
	SpatialGrid(float cellSize = 20.0f);
	void updateEntity(uint32_t entityId, const Position& oldPos, const Position& newPos);
	void addEntity(uint32_t entityId, const Position& pos);
	void removeEntity(uint32_t entityId, const Position& pos);
	std::set<uint32_t> getNearbyEntities(const Position& pos, float radius);
	void clear();

private:
	float cellSize;
	std::unordered_map<int64_t, std::set<uint32_t>> grid;
	std::mutex gridMutex;

	int64_t getCellKey(int cellX, int cellZ) const;
	void getCellCoords(const Position& pos, int& cellX, int& cellZ) const;
};
