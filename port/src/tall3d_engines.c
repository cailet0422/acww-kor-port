/* The copies of the 3D engine, in a row (gpu3d_tile.c builds one for each number). */
#include "tall3d_engine.h"

extern const Engine3d gpu3d_tile0_engine;
extern const Engine3d gpu3d_tile1_engine;
extern const Engine3d gpu3d_tile2_engine;
extern const Engine3d gpu3d_tile3_engine;
extern const Engine3d gpu3d_tile4_engine;
extern const Engine3d gpu3d_tile5_engine;
extern const Engine3d gpu3d_tile6_engine;
extern const Engine3d gpu3d_tile7_engine;
extern const Engine3d gpu3d_tile8_engine;
extern const Engine3d gpu3d_tile9_engine;
extern const Engine3d gpu3d_tile10_engine;
extern const Engine3d gpu3d_tile11_engine;
extern const Engine3d gpu3d_tile12_engine;
extern const Engine3d gpu3d_tile13_engine;
extern const Engine3d gpu3d_tile14_engine;
extern const Engine3d gpu3d_tile15_engine;
extern const Engine3d gpu3d_tile16_engine;
extern const Engine3d gpu3d_tile17_engine;

const Engine3d *const tall3d_engines[TALL3D_ENGINES] = {
    &gpu3d_tile0_engine, &gpu3d_tile1_engine, &gpu3d_tile2_engine, &gpu3d_tile3_engine, &gpu3d_tile4_engine, &gpu3d_tile5_engine, &gpu3d_tile6_engine, &gpu3d_tile7_engine, &gpu3d_tile8_engine,
    &gpu3d_tile9_engine, &gpu3d_tile10_engine, &gpu3d_tile11_engine, &gpu3d_tile12_engine, &gpu3d_tile13_engine, &gpu3d_tile14_engine, &gpu3d_tile15_engine, &gpu3d_tile16_engine, &gpu3d_tile17_engine,
};
