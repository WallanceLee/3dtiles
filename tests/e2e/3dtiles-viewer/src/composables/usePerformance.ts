import { shallowRef, onUnmounted, type Ref } from 'vue';
import * as Cesium from 'cesium';
import type { PerformanceMetrics } from '../types';

export function usePerformance(viewer: Ref<Cesium.Viewer | null>) {
  const metrics = shallowRef<PerformanceMetrics>({
    fps: 0,
    drawCalls: 0,
    triangles: 0,
    vertices: 0,
    memory: 0,
    tiles: 0,
    cacheSize: 0,
    tilesTotal: 0,
    tilesLoaded: 0,
    tilesVisible: 0,
    tilesRendered: 0,
    rootSSE: 0,
    currentGeometricError: 0
  });

  const isMonitoring = shallowRef<boolean>(false);
  let updateInterval: number | null = null;
  let frameCount = 0;
  let lastTime = performance.now();
  let removeCallback: (() => void) | null = null;

  function updateMetrics() {
    if (!viewer.value) return;

    const scene = viewer.value.scene;
    const currentTime = performance.now();
    frameCount++;

    if (currentTime - lastTime >= 1000) {
      metrics.value = {
        ...metrics.value,
        fps: frameCount
      };
      frameCount = 0;
      lastTime = currentTime;
    }

    // Extract tileset SSE and statistics
    let tilesTotal = 0, tilesLoaded = 0, tilesVisible = 0, tilesRendered = 0;
    let rootSSE = 0, currentGeometricError = 0;
    const primitives = scene.primitives;
    for (let i = 0; i < primitives.length; i++) {
      const p = primitives.get(i);
      // Cesium3DTileset has a 'root' property — use that for detection
      const root = (p as any).root;
      if (!root || !root.geometricError) continue;

      const ts = p as any;
      // statistics is a public property on Cesium3DTileset
      const stats = (ts as any).statistics;
      if (stats) {
        tilesTotal = stats.numberOfTilesTotal ?? 0;
        tilesLoaded = stats.numberOfTilesLoaded ?? 0;
        tilesVisible = stats.numberOfTilesVisited ?? 0;
        tilesRendered = stats.numberOfTilesSelected ?? 0;
      }

      // Get SSE: root tile has getScreenSpaceError publicly declared
      try {
        rootSSE = (root as any).getScreenSpaceError((scene as any).frameState, false);
      } catch {
        rootSSE = 0;
      }
      currentGeometricError = root.geometricError ?? 0;
    }

    metrics.value = {
      ...metrics.value,
      tiles: tilesTotal,
      tilesTotal,
      tilesLoaded,
      tilesVisible,
      tilesRendered,
      rootSSE: Math.round(rootSSE * 100) / 100,
      currentGeometricError,
      memory: (performance as any).memory?.usedJSHeapSize || 0
    };
  }

  function startMonitoring(interval: number = 1000) {
    if (isMonitoring.value || !viewer.value) return;

    isMonitoring.value = true;

    removeCallback = viewer.value.scene.postRender.addEventListener(updateMetrics);
    updateInterval = window.setInterval(updateMetrics, interval);
  }

  function stopMonitoring() {
    if (!isMonitoring.value) return;

    isMonitoring.value = false;

    if (removeCallback) {
      removeCallback();
      removeCallback = null;
    }

    if (updateInterval !== null) {
      clearInterval(updateInterval);
      updateInterval = null;
    }
  }

  function resetMetrics() {
    metrics.value = {
      fps: 0, drawCalls: 0, triangles: 0, vertices: 0,
      memory: 0, tiles: 0, cacheSize: 0,
      tilesTotal: 0, tilesLoaded: 0, tilesVisible: 0, tilesRendered: 0,
      rootSSE: 0, currentGeometricError: 0
    };
  }

  onUnmounted(() => {
    stopMonitoring();
  });

  return {
    metrics,
    isMonitoring,
    startMonitoring,
    stopMonitoring,
    resetMetrics
  };
}
