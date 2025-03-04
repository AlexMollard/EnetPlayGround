#pragma once
#include <cstdint>
#include <set>
#include <unordered_map>
#include <mutex>

struct Position
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	bool operator==(const Position& other) const
	{
		return x == other.x && y == other.y && z == other.z;
	}
	
	bool operator!=(const Position& other) const
	{
		return !(*this == other);
	}

	float distanceTo(const Position& other) const
	{
		float dx = x - other.x;
		float dy = y - other.y;
		float dz = z - other.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}
};

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
