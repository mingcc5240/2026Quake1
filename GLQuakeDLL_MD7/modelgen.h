/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
//
// modelgen.h: header file for model generation program
//

// *********************************************************
// * This file must be identical in the modelgen directory *
// * and in the Quake directory, because it's used to      *
// * pass data from one to the other via model files.      *
// *********************************************************

#ifdef INCLUDELIBS

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "cmdlib.h"
#include "scriplib.h"
#include "trilib.h"
#include "lbmlib.h"
#include "mathlib.h"

#endif

#define ALIAS_VERSION	6

#define ALIAS_ONSEAM				0x0020

// must match definition in spritegn.h
#ifndef SYNCTYPE_T
#define SYNCTYPE_T
typedef enum {ST_SYNC=0, ST_RAND } synctype_t;
#endif

typedef enum { ALIAS_SINGLE=0, ALIAS_GROUP } aliasframetype_t;

typedef enum { ALIAS_SKIN_SINGLE=0, ALIAS_SKIN_GROUP } aliasskintype_t;

typedef struct {
	int			ident;
	int			version;
	vec3_t		scale;
	vec3_t		scale_origin;
	float		boundingradius;
	vec3_t		eyeposition;
	int			numskins;
	int			skinwidth;
	int			skinheight;
	int			numverts;
	int			numtris;
	int			numframes;
	synctype_t	synctype;
	int			flags;
	float		size;
} mdl_t;

// TODO: could be shorts

typedef struct {
	int		onseam;
	int		s;
	int		t;
} stvert_t;

typedef struct dtriangle_s {
	int					facesfront;
	int					vertindex[3];
} dtriangle_t;

#define DT_FACES_FRONT				0x0010

// This mirrors trivert_t in trilib.h, is present so Quake knows how to
// load this data

typedef struct {
	byte	v[3];
	byte	lightnormalindex;
} trivertx_t;

typedef struct {
	trivertx_t	bboxmin;	// lightnormal isn't used
	trivertx_t	bboxmax;	// lightnormal isn't used
	char		name[16];	// frame name from grabbing
} daliasframe_t;

typedef struct {
	int			numframes;
	trivertx_t	bboxmin;	// lightnormal isn't used
	trivertx_t	bboxmax;	// lightnormal isn't used
} daliasgroup_t;

typedef struct {
	int			numskins;
} daliasskingroup_t;

typedef struct {
	float	interval;
} daliasinterval_t;

typedef struct {
	float	interval;
} daliasskininterval_t;

typedef struct {
	aliasframetype_t	type;
} daliasframetype_t;

typedef struct {
	aliasskintype_t	type;
} daliasskintype_t;

#define IDPOLYHEADER	(('O'<<24)+('P'<<16)+('D'<<8)+'I')
														// little-endian "IDPO"
// modelgen.h 에 추가

#define IDMDL3HEADER (('3'<<24)+('L'<<16)+('D'<<8)+'M') // "MDL3"
#define IDMDL4HEADER (('4'<<24)+('L'<<16)+('D'<<8)+'M') // "MDL4"
#define IDMDL5HEADER (('5'<<24)+('L'<<16)+('D'<<8)+'M') // "MDL5"

// GameStudio MDL3/4/5 공통 헤더 (Quake 1 mdl_t 기반)
typedef struct {
	int         ident;          // "MDL3", "MDL4", "MDL5"
	int         version;
	vec3_t      scale;
	vec3_t      scale_origin;
	float       boundingradius;
	vec3_t      eyeposition;
	int         numskins;
	int         skinwidth;
	int         skinheight;
	int         numverts;
	int         numtris;
	int         numframes;
	synctype_t  synctype;
	int         flags;
	float       size;
} gs_mdl_header_t;

// MDL5 전용 추가 헤더 (MDL5는 정점 수와 UV 좌표 수가 다를 수 있음)
typedef struct {
	int         num_stverts;    // ST(UV) 정점 개수
	int         unused[2];      // 예약 필드
} gs_mdl5_ext_t;

// MDL5 트라이앵글 구조체 (정점 인덱스와 UV 인덱스가 분리됨)
typedef struct {
	short       vertindex[3];
	short       stindex[3];
} gs_mdl5_triangle_t;

// modelgen.h 에 추가
#define IDMDL7HEADER (('7'<<24)+('L'<<16)+('D'<<8)+'M') // "MDL7"
// modelgen.h 에 추가
#define IDMDL3HEADER (('3'<<24)+('L'<<16)+('D'<<8)+'M') // "MDL3"

#pragma pack(push, 1)
typedef struct {
	char   id[4];           // 0..4
	int    version;         // 4..8
	int    bones_num;       // 8..12
	int    groups_num;      // 12..16
	int    data_size;       // 16..20
	int    entlump_size;    // 20..24
	int    medlump_size;    // 24..28
	short  bone_trf_size;   // 28..30
	short  skin_size;       // 30..32
	short  colorvalue_size; // 32..34 (16)
	short  material_size;   // 34..36 (68)
	short  skinpoint_size;  // 36..38 (8)
	short  triangle_size;   // 38..40 (16)
	short  mainvertex_size; // 40..42 (26)
	short  framevertex_size;// 42..44
	short  bonetrans_size;  // 44..46
	short  frame_size;      // 46..48
} mdl7_header_t;

typedef struct {
	byte   typ;
	byte   deformers;
	byte   max_weights;
	byte   unused;
	int    groupdata_size;
	char   name[16];
	int    numskins;
	int    num_stpts;
	int    numtris;
	int    numverts;
	int    numframes;
} mdl7_group_t;

typedef struct {
	byte   typ;             // 하위 4비트: 포맷(2=16bit, 3=24bit, 4=32bit, 6=DDS 등)
	byte   unused[3];
	int    width;
	int    height;
	char   texture_name[16];
} mdl7_skin_t;

typedef struct {
	float  u, v;
} mdl7_skinpt_t;

typedef struct {
	short  v_index[3];
	short  skinsets[1][3];  // 기본 1개 skinset 기준
} mdl7_triangle_t;

typedef struct {
	float  x, y, z;
} mdl7_vec3_t;

// modelgen.h 에 추가 (#pragma pack(push, 1) 내부)
typedef struct {
	char   name[16];
	int    vertices_count;      // 이 프레임에 포함된 정점 수
	int    transmatrix_count;   // 본 변환 행렬 수
} mdl7_frame_t;

typedef struct {
	float          x, y, z;
	unsigned short vertindex;   // 변경할 Main Vertex 인덱스
	float          nx, ny, nz;  // 법선 벡터
} mdl7_framevert_t;

typedef struct {
	char   id[4];           // "MDL3"
	int    version;
	vec3_t scale;
	vec3_t scale_origin;
	float  boundingradius;
	vec3_t eyeposition;
	int    numskins;
	int    skinwidth;
	int    skinheight;
	int    numverts;
	int    numtris;
	int    numframes;
	int    numskinverts;    // 기존 Quake 1의 synctype 위치
	int    flags;
	float  size;
} mdl3_header_t;

typedef struct {
	short  u, v;            // 픽셀 단위 UV (4바이트)
} mdl3_uvvert_t;

typedef struct {
	short  index_xyz[3];    // 3D 정점 인덱스
	short  index_uv[3];     // UV 정점 인덱스
} mdl3_triangle_t;
#pragma pack(pop)