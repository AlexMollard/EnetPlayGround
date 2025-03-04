#include "SpatialGrid.h"

SpatialGrid::SpatialGrid(float cellSize)
      : cellSize(cellSize)
{
}

int64_t SpatialGrid::getCellKey(int cellX, int cellZ) const
{
	// Combine cell X,Z into a single 64-bit key
	return (static_cast<int64_t>(cellX) << 32) | static_cast<uint32_t>(cellZ);
}

void SpatialGrid::getCellCoords(const Position& pos, int& cellX, int& cellZ) const
{
	cellX = static_cast<int>(std::floor(pos.x / cellSize));
	cellZ = static_cast<int>(std::floor(pos.z / cellSize));
}

void SpatialGrid::updateEntity(uint32_t entityId, const Position& oldPos, const Position& newPos)
{
	std::lock_guard<std::mutex> lock(gridMutex);

	int oldCellX, oldCellZ;
	int newCellX, newCellZ;

	getCellCoords(oldPos, oldCellX, oldCellZ);
	getCellCoords(newPos, newCellX, newCellZ);

	// Check if cell has changed
	if (oldCellX != newCellX || oldCellZ != newCellZ)
	{
		// Remove from old cell
		auto oldCellKey = getCellKey(oldCellX, oldCellZ);
		auto oldCellIt = grid.find(oldCellKey);
		if (oldCellIt != grid.end())
		{
			oldCellIt->second.erase(entityId);

			// Remove empty cell
			if (oldCellIt->second.empty())
			{
				grid.erase(oldCellIt);
			}
		}

		// Add to new cell
		auto newCellKey = getCellKey(newCellX, newCellZ);
		grid[newCellKey].insert(entityId);
	}
}

void SpatialGrid::addEntity(uint32_t entityId, const Position& pos)
{
	std::lock_guard<std::mutex> lock(gridMutex);

	int cellX, cellZ;
	getCellCoords(pos, cellX, cellZ);

	auto cellKey = getCellKey(cellX, cellZ);
	grid[cellKey].insert(entityId);
}

void SpatialGrid::removeEntity(uint32_t entityId, const Position& pos)
{
	std::lock_guard<std::mutex> lock(gridMutex);

	int cellX, cellZ;
	getCellCoords(pos, cellX, cellZ);

	auto cellKey = getCellKey(cellX, cellZ);
	auto cellIt = grid.find(cellKey);
	if (cellIt != grid.end())
	{
		cellIt->second.erase(entityId);

		// Remove empty cell
		if (cellIt->second.empty())
		{
			grid.erase(cellIt);
		}
	}
}

std::set<uint32_t> SpatialGrid::getNearbyEntities(const Position& pos, float radius)
{
	std::lock_guard<std::mutex> lock(gridMutex);
	std::set<uint32_t> result;

	// Calculate cell range to check
	int cellRadius = static_cast<int>(std::ceil(radius / cellSize));
	int centerCellX, centerCellZ;
	getCellCoords(pos, centerCellX, centerCellZ);

	// Iterate through cells in range
	for (int dz = -cellRadius; dz <= cellRadius; ++dz)
	{
		for (int dx = -cellRadius; dx <= cellRadius; ++dx)
		{
			int cellX = centerCellX + dx;
			int cellZ = centerCellZ + dz;

			auto cellKey = getCellKey(cellX, cellZ);
			auto cellIt = grid.find(cellKey);
			if (cellIt != grid.end())
			{
				result.insert(cellIt->second.begin(), cellIt->second.end());
			}
		}
	}

	return result;
}

void SpatialGrid::clear()
{
	std::lock_guard<std::mutex> lock(gridMutex);
	grid.clear();
}
