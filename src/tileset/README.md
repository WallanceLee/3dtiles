# Tileset Module

This module provides a unified C++ interface for generating 3D Tiles `tileset.json` files.

## Overview

The tileset module consolidates the previously scattered tileset generation logic from:
- `shp23dtile` (Shapefile to 3D Tiles)
- `osgb23dtile` (OSGB to 3D Tiles)  
- `fbx23dtile` (FBX to 3D Tiles)

## Architecture

```
tileset/
├── bounding_volume.h/cpp    # Bounding volume types (Box, Region, Sphere)
├── geometric_error.h/cpp    # Geometric error calculations
├── transform.h/cpp          # Coordinate transformations (ENU/ECEF)
├── tileset_types.h/cpp      # Core types (Tile, Tileset, Content, Asset)
├── tileset_writer.h/cpp     # JSON serialization
└── tileset.h                # Main header (includes all above)
```

## Quick Start

```cpp
#include "tileset/tileset.h"

using namespace tileset;

// Create a bounding box
Box box = Box::fromCenterAndHalfLengths(0, 0, 50, 100, 100, 50);

// Create a tile
Tile tile(box);
tile.geometricError = computeGeometricError(box);
tile.setContent("model.b3dm");

// Create a tileset
Tileset tileset(tile);
tileset.setVersion("1.0");
tileset.setGltfUpAxis("Z");

// Write to file
TilesetWriter writer;
writer.writeToFile(tileset, "tileset.json");
```

## Core Concepts

### Bounding Volumes

Three types of bounding volumes are supported:

```cpp
// Box: center(3) + x_axis(3) + y_axis(3) + z_axis(3) = 12 numbers
Box box = Box::fromCenterAndHalfLengths(cx, cy, cz, hx, hy, hz);

// Region: west, south, east, north, min_height, max_height (in radians)
Region region = Region::fromDegrees(west, south, east, north, min_h, max_h);

// Sphere: center(3) + radius = 4 numbers
Sphere sphere = Sphere::fromCenterAndRadius(cx, cy, cz, radius);
```

### Geometric Error

Geometric error determines when a tile is refined:

```cpp
// From bounding volume
double error = computeGeometricError(boundingVolume);

// From dimensions
double error = computeGeometricErrorFromBox(width, height, depth);

// From diagonal
double error = computeGeometricErrorFromDiagonal(diagonal);
```

### Coordinate Transforms

ENU (East-North-Up) to ECEF (Earth-Centered-Earth-Fixed):

```cpp
// Calculate transform matrix at a geodetic position
TransformMatrix matrix = calcEnuToEcefMatrix(lon_deg, lat_deg, height);

// Apply to tile
tile.setTransform(matrix);
```

### Hierarchical Tilesets

```cpp
// Create parent tile
Tile parent(box);
parent.geometricError = 100.0;

// Create child tiles
Tile child1(childBox1);
child1.geometricError = 50.0;
child1.setContent("child1.b3dm");

Tile child2(childBox2);
child2.geometricError = 50.0;
child2.setContent("child2.b3dm");

// Build hierarchy
parent.addChild(child1);
parent.addChild(child2);
parent.refine = "REPLACE"; // or "ADD"

// Create tileset
Tileset tileset(parent);
```

## Migration Guide

### From old tileset.cpp

**Before:**
```cpp
Box box = {...};
write_tileset_box(&transform, box, geometricError, "model.b3dm", "tileset.json");
```

**After:**
```cpp
Box box = Box::fromCenterAndHalfLengths(...);
Tile tile(box);
tile.geometricError = geometricError;
tile.setContent("model.b3dm");
if (transform) {
    tile.setTransform(*transform);
}

Tileset tileset(tile);
TilesetWriter writer;
writer.writeToFile(tileset, "tileset.json");
```

### From shp23dtile

**Before:**
```cpp
nlohmann::json root;
root["asset"] = {{"version", "1.0"}, {"gltfUpAxis", "Z"}};
root["geometricError"] = node.geometric_error;
// ... manual JSON construction
```

**After:**
```cpp
Tile tile(box);
tile.geometricError = node.geometric_error;
// ... use TilesetWriter
```

## API Reference

See header files for detailed API documentation.

## Dependencies

- nlohmann/json (JSON serialization)
- Standard C++17/20 library
