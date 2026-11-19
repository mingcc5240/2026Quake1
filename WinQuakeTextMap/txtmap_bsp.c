// txtmap_bsp.c - Pure C in-memory .txt -> BSP29 builder for WinQuake
#include "quakedef.h"

#define TXTMAP_MAX_GRID   64
#define TXTMAP_TILE       128.0f
#define TXTMAP_MIN_Z      -64.0f   // 하천 바닥 아래 최소 높이
#define TXTMAP_SKY_HEIGHT 320.0f   // 타워(224)가 들어갈 수 있는 하늘 높이
#define TXTMAP_HEIGHT 64.0f

// 동적 버퍼 헬퍼
typedef struct {
	byte* data;
	int  len;
	int  cap;
} membuf_t;



static void mb_init(membuf_t* b, int cap)
{
	b->data = (byte*)malloc(cap);
	b->len = 0;
	b->cap = cap;
}

static void mb_free(membuf_t* b)
{
	if (b->data) free(b->data);
	b->data = NULL;
	b->len = b->cap = 0;
}

static int mb_append(membuf_t* b, const void* src, int size)
{
	int ofs = b->len;
	if (b->len + size > b->cap)
	{
		while (b->len + size > b->cap)
			b->cap = b->cap ? b->cap * 2 : 4096;
		b->data = (byte*)realloc(b->data, b->cap);
	}
	if (src)
		memcpy(b->data + b->len, src, size);
	else
		memset(b->data + b->len, 0, size);
	b->len += size;
	return ofs;
}

// BSP 빌더 상태
typedef struct {
	char grid[TXTMAP_MAX_GRID][TXTMAP_MAX_GRID];
	int  rows, cols;

	membuf_t entities;
	membuf_t planes;
	membuf_t textures;
	membuf_t vertexes;
	membuf_t nodes;
	membuf_t texinfo;
	membuf_t faces;
	membuf_t clipnodes;
	membuf_t leafs;
	membuf_t marksurfaces;
	membuf_t edges;
	membuf_t surfedges;
	membuf_t models;
} bspbuilder_t;

static int add_plane(bspbuilder_t* b, int axis, float dist)
{
	dplane_t p;
	memset(&p, 0, sizeof(p));
	p.normal[axis] = 1.0f;
	p.dist = dist;
	p.type = axis;
	return mb_append(&b->planes, &p, sizeof(p)) / (int)sizeof(dplane_t);
}

static int add_vertex(bspbuilder_t* b, float x, float y, float z)
{
	dvertex_t v;
	v.point[0] = x;
	v.point[1] = y;
	v.point[2] = z;
	return mb_append(&b->vertexes, &v, sizeof(v)) / (int)sizeof(dvertex_t);
}

static int add_edge(bspbuilder_t* b, int v0, int v1)
{
	dedge_t e;
	e.v[0] = (unsigned short)v0;
	e.v[1] = (unsigned short)v1;
	return mb_append(&b->edges, &e, sizeof(e)) / (int)sizeof(dedge_t);
}

// 4개 정점으로 사각형 면(dface_t) 생성
static int add_quad(bspbuilder_t* b, int planenum, int side, int texinfo_idx,
	float v0[3], float v1[3], float v2[3], float v3[3])
{
	int iv0 = add_vertex(b, v0[0], v0[1], v0[2]);
	int iv1 = add_vertex(b, v1[0], v1[1], v1[2]);
	int iv2 = add_vertex(b, v2[0], v2[1], v2[2]);
	int iv3 = add_vertex(b, v3[0], v3[1], v3[2]);

	int e0 = add_edge(b, iv0, iv1);
	int e1 = add_edge(b, iv1, iv2);
	int e2 = add_edge(b, iv2, iv3);
	int e3 = add_edge(b, iv3, iv0);

	int firstedge = b->surfedges.len / (int)sizeof(int);
	mb_append(&b->surfedges, &e0, sizeof(int));
	mb_append(&b->surfedges, &e1, sizeof(int));
	mb_append(&b->surfedges, &e2, sizeof(int));
	mb_append(&b->surfedges, &e3, sizeof(int));

	dface_t f;
	memset(&f, 0, sizeof(f));
	f.planenum = (short)planenum;
	f.side = (short)side;
	f.firstedge = firstedge;
	f.numedges = 4;
	f.texinfo = (short)texinfo_idx;
	f.styles[0] = 0;
	f.styles[1] = 255;
	f.styles[2] = 255;
	f.styles[3] = 255;
	f.lightofs = -1; // Fullbright (라이트맵 없이 밝게 표시)

	return mb_append(&b->faces, &f, sizeof(f)) / (int)sizeof(dface_t);
}

//
// WAD2 파일에서 특정 이름의 Miptex(0x44) 원본 바이트를 읽어오는 헬퍼
static byte* load_wad_miptex(const char* wad_path, const char* tex_name, int* out_size)
{
	FILE* fp;
	char magic[4];
	int num_entries, dir_ofs, i;

	fp = fopen(wad_path, "rb");
	if (!fp)
		return NULL;

	if (fread(magic, 1, 4, fp) != 4 || memcmp(magic, "WAD2", 4) != 0)
	{
		fclose(fp);
		return NULL;
	}
	fread(&num_entries, 4, 1, fp);
	fread(&dir_ofs, 4, 1, fp);

	fseek(fp, dir_ofs, SEEK_SET);
	for (i = 0; i < num_entries; i++)
	{
		int filepos, disksize, size;
		byte type, compression;
		unsigned short pad;
		char name[17];

		fread(&filepos, 4, 1, fp);
		fread(&disksize, 4, 1, fp);
		fread(&size, 4, 1, fp);
		fread(&type, 1, 1, fp);
		fread(&compression, 1, 1, fp);
		fread(&pad, 2, 1, fp);
		fread(name, 1, 16, fp);
		name[16] = 0;

		if (type == 0x44 && Q_strcasecmp(name, tex_name) == 0)
		{
			byte* buf = (byte*)malloc(disksize);
			if (buf)
			{
				fseek(fp, filepos, SEEK_SET);
				fread(buf, 1, disksize, fp);
				*out_size = disksize;
			}
			fclose(fp);
			return buf;
		}
	}

	fclose(fp);
	return NULL;
}

static void build_procedural_textures(bspbuilder_t* b)
{
	// base.wad 안에서 불러올 텍스처 이름 (0:바닥, 1:벽, 2:천장, 3:물)
	const char* names[4] = { "sfloor4_1", "tech01_1", "sky4", "*water1" };
	char wad_path[260];
	byte* tex_data[4] = { NULL, NULL, NULL, NULL };
	int tex_size[4] = { 0, 0, 0, 0 };
	int offsets[4];
	int num = 4;
	int header_size = sizeof(int) + num * sizeof(int);
	int cur_ofs = header_size;
	int t, i, x, y;

	// 1. id1/base.wad 우선 탐색, 없으면 현재 폴더의 base.wad 탐색
	sprintf(wad_path, "%s/base.wad", com_gamedir);
	for (t = 0; t < num; t++)
	{
		tex_data[t] = load_wad_miptex(wad_path, names[t], &tex_size[t]);
		if (!tex_data[t])
			tex_data[t] = load_wad_miptex("base.wad", names[t], &tex_size[t]);

		// WAD에서 못 찾았으면 64x64 자체 텍스처 생성
		if (!tex_data[t])
		{
			int mip_total = 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8;
			int total = (int)sizeof(miptex_t) + mip_total;
			miptex_t* mt;
			byte* p64, * p32, * p16, * p8;

			tex_data[t] = (byte*)calloc(1, total);
			tex_size[t] = total;
			mt = (miptex_t*)tex_data[t];
			strncpy(mt->name, names[t], 15);
			mt->width = 64;
			mt->height = 64;
			mt->offsets[0] = sizeof(miptex_t);
			mt->offsets[1] = mt->offsets[0] + 64 * 64;
			mt->offsets[2] = mt->offsets[1] + 32 * 32;
			mt->offsets[3] = mt->offsets[2] + 16 * 16;

			p64 = tex_data[t] + mt->offsets[0];
			p32 = tex_data[t] + mt->offsets[1];
			p16 = tex_data[t] + mt->offsets[2];
			p8 = tex_data[t] + mt->offsets[3];

			for (y = 0; y < 64; y++)
			{
				for (x = 0; x < 64; x++)
				{
					if (t == 0)      // 바닥
						p64[y * 64 + x] = (x == 0 || y == 0 || x == 32 || y == 32) ? 6 : 10;
					else if (t == 1) // 벽
						p64[y * 64 + x] = (y % 16 == 0 || ((x + ((y / 16) & 1) * 16) % 32 == 0)) ? 4 : 24;
					else if (t == 2) // 천장
						p64[y * 64 + x] = ((x & 31) == 0 || (y & 31) == 0) ? 38 : 42;
					else             // 물 (파란 물결 톤)
						p64[y * 64 + x] = (((x + y) & 15) == 0) ? 210 : 214;
				}
			}

			for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) p32[y * 32 + x] = p64[(y * 2) * 64 + (x * 2)];
			for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) p16[y * 16 + x] = p32[(y * 2) * 32 + (x * 2)];
			for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) p8[y * 8 + x] = p16[(y * 2) * 16 + (x * 2)];
		}

		// 4바이트 정렬된 오프셋 계산
		offsets[t] = cur_ofs;
		cur_ofs += (tex_size[t] + 3) & ~3;
	}

	// 2. LUMP_TEXTURES 조립
	mb_append(&b->textures, &num, sizeof(int));
	mb_append(&b->textures, offsets, sizeof(offsets));
	for (t = 0; t < num; t++)
	{
		mb_append(&b->textures, tex_data[t], tex_size[t]);
		while (b->textures.len & 3)
			mb_append(&b->textures, "", 1);
		free(tex_data[t]);
	}

	// 3. Texinfo 5종 (0:바닥, 1:천장, 2:X벽, 3:Y벽, 4:물)
	for (i = 0; i < 5; i++)
	{
		texinfo_t ti;
		memset(&ti, 0, sizeof(ti));
		if (i == 0) { ti.vecs[0][0] = 1; ti.vecs[1][1] = 1;  ti.miptex = 0; }
		else if (i == 1) { ti.vecs[0][0] = 1; ti.vecs[1][1] = 1;  ti.miptex = 2; }
		else if (i == 2) { ti.vecs[0][1] = 1; ti.vecs[1][2] = -1; ti.miptex = 1; }
		else if (i == 3) { ti.vecs[0][0] = 1; ti.vecs[1][2] = -1; ti.miptex = 1; }
		else { ti.vecs[0][0] = 1; ti.vecs[1][1] = 1;  ti.miptex = 3; } // 물 (*water1)
		mb_append(&b->texinfo, &ti, sizeof(ti));
	}
}

//
/*
// 64x64 절차적 텍스처 3종(0:바닥, 1:벽, 2:천장) 생성
static void build_procedural_textures(bspbuilder_t* b)
{
	const char* names[3] = { "txt_floor", "txt_wall", "txt_ceil" };
	int header_size = sizeof(int) + 3 * sizeof(int);
	int mip_total = 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8;
	int t, i, x, y;
	int num = 3;
	int offsets[3];

	for (t = 0; t < 3; t++)
		offsets[t] = header_size + t * ((int)sizeof(miptex_t) + mip_total);

	mb_append(&b->textures, &num, sizeof(int));
	mb_append(&b->textures, offsets, sizeof(offsets));

	for (t = 0; t < 3; t++)
	{
		miptex_t mt;
		byte pix64[64 * 64];
		byte pix32[32 * 32];
		byte pix16[16 * 16];
		byte pix8[8 * 8];

		memset(&mt, 0, sizeof(mt));
		strcpy(mt.name, names[t]);
		mt.width = 64;
		mt.height = 64;
		mt.offsets[0] = sizeof(miptex_t);
		mt.offsets[1] = mt.offsets[0] + 64 * 64;
		mt.offsets[2] = mt.offsets[1] + 32 * 32;
		mt.offsets[3] = mt.offsets[2] + 16 * 16;

		for (y = 0; y < 64; y++)
		{
			for (x = 0; x < 64; x++)
			{
				byte col;
				if (t == 0) // 바닥: 사각 타일 패턴
					col = (x == 0 || y == 0 || x == 32 || y == 32) ? 6 : 10;
				else if (t == 1) // 벽: 벽돌 패턴
				{
					int row = y / 16;
					int bx = (x + (row & 1) * 16) % 32;
					col = (y % 16 == 0 || bx == 0) ? 4 : 24;
				}
				else // 천장: 부드러운 회청색 격자
					col = ((x & 31) == 0 || (y & 31) == 0) ? 38 : 42;
				pix64[y * 64 + x] = col;
			}
		}

		// Mipmap 다운샘플링
		for (y = 0; y < 32; y++)
			for (x = 0; x < 32; x++)
				pix32[y * 32 + x] = pix64[(y * 2) * 64 + (x * 2)];
		for (y = 0; y < 16; y++)
			for (x = 0; x < 16; x++)
				pix16[y * 16 + x] = pix32[(y * 2) * 32 + (x * 2)];
		for (y = 0; y < 8; y++)
			for (x = 0; x < 8; x++)
				pix8[y * 8 + x] = pix16[(y * 2) * 16 + (x * 2)];

		mb_append(&b->textures, &mt, sizeof(mt));
		mb_append(&b->textures, pix64, sizeof(pix64));
		mb_append(&b->textures, pix32, sizeof(pix32));
		mb_append(&b->textures, pix16, sizeof(pix16));
		mb_append(&b->textures, pix8, sizeof(pix8));
	}

	// Texinfo 4종: 0=바닥(XY), 1=천장(XY), 2=X벽(YZ), 3=Y벽(XZ)
	for (i = 0; i < 4; i++)
	{
		texinfo_t ti;
		memset(&ti, 0, sizeof(ti));
		if (i == 0) { ti.vecs[0][0] = 1; ti.vecs[1][1] = 1; ti.miptex = 0; }
		else if (i == 1) { ti.vecs[0][0] = 1; ti.vecs[1][1] = 1; ti.miptex = 2; }
		else if (i == 2) { ti.vecs[0][1] = 1; ti.vecs[1][2] = -1; ti.miptex = 1; }
		else { ti.vecs[0][0] = 1; ti.vecs[1][2] = -1; ti.miptex = 1; }
		mb_append(&b->texinfo, &ti, sizeof(ti));
	}
} */

static float get_tile_z(bspbuilder_t* b, int c, int r)
{
	char ch;
	if (c < 0 || c >= b->cols || r < 0 || r >= b->rows)
		return TXTMAP_SKY_HEIGHT;

	ch = b->grid[r][c];
	// [추가] 맵 외곽 테두리(#)는 하늘 천장까지 막아 회색 빈 공간 방지
	if (ch == '#' && (c == 0 || c == b->cols - 1 || r == 0 || r == b->rows - 1))
		return TXTMAP_SKY_HEIGHT;

	if (ch == 'O') return 224.0f; // 타워
	if (ch == '#') return 128.0f; // 내부 벽
	if (ch == '+') return 64.0f;  // 2층 지대
	if (ch == '~') return -48.0f; // 하천
	return 0.0f;
}


static qboolean is_wall(bspbuilder_t* b, int c, int r)
{
	if (c < 0 || c >= b->cols || r < 0 || r >= b->rows)
		return true;
	return b->grid[r][c] == '#';
}

static short build_grid_bsp(bspbuilder_t* b, int c0, int c1, int r0, int r1)
{
	if (c1 - c0 > 1)
	{
		int cm = (c0 + c1) / 2;
		float x = cm * TXTMAP_TILE;
		int pnum = add_plane(b, PLANE_X, x);
		int node_idx = mb_append(&b->nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
		int firstface = b->faces.len / (int)sizeof(dface_t);
		int numfaces = 0;
		int r;

		// X 분할선 양쪽의 높이 차이만큼 수직 벽면 생성
		for (r = r0; r < r1; r++)
		{
			float zl = get_tile_z(b, cm - 1, r);
			float zr = get_tile_z(b, cm, r);
			if (zl != zr)
			{
				float y0 = r * TXTMAP_TILE, y1 = (r + 1) * TXTMAP_TILE;
				if (zl > zr) // 왼쪽이 더 높음 -> +X 방향(side=0) 벽면
				{
					float a[3] = { x,y0,zr }, bb[3] = { x,y0,zl }, c[3] = { x,y1,zl }, d[3] = { x,y1,zr };
					add_quad(b, pnum, 0, 2, a, bb, c, d);
				}
				else // 오른쪽이 더 높음 -> -X 방향(side=1) 벽면
				{
					float a[3] = { x,y1,zl }, bb[3] = { x,y1,zr }, c[3] = { x,y0,zr }, d[3] = { x,y0,zl };
					add_quad(b, pnum, 1, 2, a, bb, c, d);
				}
				numfaces++;
			}
		}

		{
			short front = build_grid_bsp(b, cm, c1, r0, r1);
			short back = build_grid_bsp(b, c0, cm, r0, r1);
			dnode_t* n = ((dnode_t*)b->nodes.data) + node_idx;
			n->planenum = pnum;
			n->children[0] = front;
			n->children[1] = back;
			n->mins[0] = (short)(c0 * TXTMAP_TILE); n->mins[1] = (short)(r0 * TXTMAP_TILE); n->mins[2] = (short)TXTMAP_MIN_Z;
			n->maxs[0] = (short)(c1 * TXTMAP_TILE); n->maxs[1] = (short)(r1 * TXTMAP_TILE); n->maxs[2] = (short)TXTMAP_SKY_HEIGHT;
			n->firstface = (unsigned short)firstface;
			n->numfaces = (unsigned short)numfaces;
		}
		return (short)node_idx;
	}

	if (r1 - r0 > 1)
	{
		int rm = (r0 + r1) / 2;
		float y = rm * TXTMAP_TILE;
		int pnum = add_plane(b, PLANE_Y, y);
		int node_idx = mb_append(&b->nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
		int firstface = b->faces.len / (int)sizeof(dface_t);
		int numfaces = 0;
		int c;

		// Y 분할선 양쪽의 높이 차이만큼 수직 벽면 생성
		for (c = c0; c < c1; c++)
		{
			float zd = get_tile_z(b, c, rm - 1);
			float zu = get_tile_z(b, c, rm);
			if (zd != zu)
			{
				float x0 = c * TXTMAP_TILE, x1 = (c + 1) * TXTMAP_TILE;
				if (zd > zu) // 아래쪽이 더 높음 -> +Y 방향(side=0) 벽면
				{
					float a[3] = { x1,y,zu }, bb[3] = { x1,y,zd }, c[3] = { x0,y,zd }, d[3] = { x0,y,zu };
					add_quad(b, pnum, 0, 3, a, bb, c, d);
				}
				else // 위쪽이 더 높음 -> -Y 방향(side=1) 벽면
				{
					float a[3] = { x0,y,zd }, bb[3] = { x0,y,zu }, c[3] = { x1,y,zu }, d[3] = { x1,y,zd };
					add_quad(b, pnum, 1, 3, a, bb, c, d);
				}
				numfaces++;
			}
		}

		{
			short front = build_grid_bsp(b, c0, c1, rm, r1);
			short back = build_grid_bsp(b, c0, c1, r0, rm);
			dnode_t* n = ((dnode_t*)b->nodes.data) + node_idx;
			n->planenum = pnum;
			n->children[0] = front;
			n->children[1] = back;
			n->mins[0] = (short)(c0 * TXTMAP_TILE); n->mins[1] = (short)(r0 * TXTMAP_TILE); n->mins[2] = (short)TXTMAP_MIN_Z;
			n->maxs[0] = (short)(c1 * TXTMAP_TILE); n->maxs[1] = (short)(r1 * TXTMAP_TILE); n->maxs[2] = (short)TXTMAP_SKY_HEIGHT;
			n->firstface = (unsigned short)firstface;
			n->numfaces = (unsigned short)numfaces;
		}
		return (short)node_idx;
	}

	/*
	// 단일 타일(c0, r0): 타일 높이(fz)에 수평 바닥/상판 노드 생성
	{
		float x0 = c0 * TXTMAP_TILE, x1 = (c0 + 1) * TXTMAP_TILE;
		float y0 = r0 * TXTMAP_TILE, y1 = (r0 + 1) * TXTMAP_TILE;
		float fz = get_tile_z(b, c0, r0);
		int pnum = add_plane(b, PLANE_Z, fz);
		int node_idx = mb_append(&b->nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
		int firstface = b->faces.len / (int)sizeof(dface_t);

		// 타일 상단 면 (normal +Z)
		{
			float a[3] = { x0,y0,fz }, bb[3] = { x0,y1,fz }, cc[3] = { x1,y1,fz }, d[3] = { x1,y0,fz };
			add_quad(b, pnum, 0, 0, a, bb, cc, d);
		}

		// 위쪽 공간은 빈 리프(CONTENTS_EMPTY)
		dleaf_t lf;
		memset(&lf, 0, sizeof(lf));
		lf.contents = CONTENTS_EMPTY;
		lf.visofs = -1;
		lf.mins[0] = (short)x0; lf.mins[1] = (short)y0; lf.mins[2] = (short)fz;
		lf.maxs[0] = (short)x1; lf.maxs[1] = (short)y1; lf.maxs[2] = (short)TXTMAP_SKY_HEIGHT;
		int leaf_idx = mb_append(&b->leafs, &lf, sizeof(lf)) / (int)sizeof(dleaf_t);

		dnode_t* n = ((dnode_t*)b->nodes.data) + node_idx;
		n->planenum = pnum;
		n->children[0] = (short)(-(leaf_idx + 1)); // 위쪽: EMPTY
		n->children[1] = -1;                       // 아래쪽: SOLID
		n->mins[0] = (short)x0; n->mins[1] = (short)y0; n->mins[2] = (short)TXTMAP_MIN_Z;
		n->maxs[0] = (short)x1; n->maxs[1] = (short)y1; n->maxs[2] = (short)TXTMAP_SKY_HEIGHT;
		n->firstface = (unsigned short)firstface;
		n->numfaces = 1;
		return (short)node_idx;
	} */
	//
	// 단일 타일(c0, r0): 타일 높이(fz)에 수평 바닥/상판 (하천이면 수면 포함) 노드 생성
	{
		float x0 = c0 * TXTMAP_TILE, x1 = (c0 + 1) * TXTMAP_TILE;
		float y0 = r0 * TXTMAP_TILE, y1 = (r0 + 1) * TXTMAP_TILE;
		float fz = get_tile_z(b, c0, r0);

		if (b->grid[r0][c0] == '~')
		{
			// 하천(~): z = -8.0f에 *water1 수면 + 그 아래(-48 ~ -8)에 CONTENTS_WATER 생성
			float wz = -8.0f;
			int p_water = add_plane(b, PLANE_Z, wz);
			int p_floor = add_plane(b, PLANE_Z, fz);

			// 부모 인덱스가 자식보다 작도록 수면 노드를 먼저 할당
			int water_node_idx = mb_append(&b->nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
			int floor_node_idx = mb_append(&b->nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);

			// 1. 수면 면 생성 (texinfo = 4: *water1, 위/아래 양면)
			int water_first = b->faces.len / (int)sizeof(dface_t);
			{
				float a[3] = { x0,y0,wz }, bb[3] = { x0,y1,wz }, cc[3] = { x1,y1,wz }, d[3] = { x1,y0,wz };
				add_quad(b, p_water, 0, 4, a, bb, cc, d); // 위에서 보는 수면 (+Z)
				add_quad(b, p_water, 1, 4, a, d, cc, bb); // 물속에서 올려다보는 수면 (-Z)
			}

			// 2. 강바닥 면 생성 (texinfo = 0: 일반 바닥)
			int floor_first = b->faces.len / (int)sizeof(dface_t);
			{
				float a[3] = { x0,y0,fz }, bb[3] = { x0,y1,fz }, cc[3] = { x1,y1,fz }, d[3] = { x1,y0,fz };
				add_quad(b, p_floor, 0, 0, a, bb, cc, d);
			}

			// 3. 공기 리프 (수면 위: -8 ~ SKY) 및 물 리프 (수면 아래: -48 ~ -8)
			dleaf_t lf_air, lf_water;
			memset(&lf_air, 0, sizeof(lf_air));
			lf_air.contents = CONTENTS_EMPTY;
			lf_air.visofs = -1;
			lf_air.mins[0] = (short)x0; lf_air.mins[1] = (short)y0; lf_air.mins[2] = (short)wz;
			lf_air.maxs[0] = (short)x1; lf_air.maxs[1] = (short)y1; lf_air.maxs[2] = (short)TXTMAP_SKY_HEIGHT;
			int air_leaf_idx = mb_append(&b->leafs, &lf_air, sizeof(lf_air)) / (int)sizeof(dleaf_t);

			memset(&lf_water, 0, sizeof(lf_water));
			lf_water.contents = CONTENTS_WATER;
			lf_water.visofs = -1;
			lf_water.mins[0] = (short)x0; lf_water.mins[1] = (short)y0; lf_water.mins[2] = (short)fz;
			lf_water.maxs[0] = (short)x1; lf_water.maxs[1] = (short)y1; lf_water.maxs[2] = (short)wz;
			int water_leaf_idx = mb_append(&b->leafs, &lf_water, sizeof(lf_water)) / (int)sizeof(dleaf_t);

			// 4. 노드 연결 (수면 노드 -> 위: 공기, 아래: 강바닥 노드 -> 위: 물, 아래: SOLID)
			dnode_t* wn = ((dnode_t*)b->nodes.data) + water_node_idx;
			wn->planenum = p_water;
			wn->children[0] = (short)(-(air_leaf_idx + 1));
			wn->children[1] = (short)floor_node_idx;
			wn->mins[0] = (short)x0; wn->mins[1] = (short)y0; wn->mins[2] = (short)TXTMAP_MIN_Z;
			wn->maxs[0] = (short)x1; wn->maxs[1] = (short)y1; wn->maxs[2] = (short)TXTMAP_SKY_HEIGHT;
			wn->firstface = (unsigned short)water_first;
			wn->numfaces = 2;

			dnode_t* fn = ((dnode_t*)b->nodes.data) + floor_node_idx;
			fn->planenum = p_floor;
			fn->children[0] = (short)(-(water_leaf_idx + 1));
			fn->children[1] = -1; // SOLID
			fn->mins[0] = (short)x0; fn->mins[1] = (short)y0; fn->mins[2] = (short)TXTMAP_MIN_Z;
			fn->maxs[0] = (short)x1; fn->maxs[1] = (short)y1; fn->maxs[2] = (short)wz;
			fn->firstface = (unsigned short)floor_first;
			fn->numfaces = 1;

			return (short)water_node_idx;
		}
		else
		{
			// 일반 타일 (., +, #, O 등)
			int pnum = add_plane(b, PLANE_Z, fz);
			int node_idx = mb_append(&b->nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
			int firstface = b->faces.len / (int)sizeof(dface_t);

			{
				float a[3] = { x0,y0,fz }, bb[3] = { x0,y1,fz }, cc[3] = { x1,y1,fz }, d[3] = { x1,y0,fz };
				add_quad(b, pnum, 0, 0, a, bb, cc, d);
			}

			dleaf_t lf;
			memset(&lf, 0, sizeof(lf));
			lf.contents = CONTENTS_EMPTY;
			lf.visofs = -1;
			lf.mins[0] = (short)x0; lf.mins[1] = (short)y0; lf.mins[2] = (short)fz;
			lf.maxs[0] = (short)x1; lf.maxs[1] = (short)y1; lf.maxs[2] = (short)TXTMAP_SKY_HEIGHT;
			int leaf_idx = mb_append(&b->leafs, &lf, sizeof(lf)) / (int)sizeof(dleaf_t);

			dnode_t* n = ((dnode_t*)b->nodes.data) + node_idx;
			n->planenum = pnum;
			n->children[0] = (short)(-(leaf_idx + 1)); // 위쪽: EMPTY
			n->children[1] = -1;                       // 아래쪽: SOLID
			n->mins[0] = (short)x0; n->mins[1] = (short)y0; n->mins[2] = (short)TXTMAP_MIN_Z;
			n->maxs[0] = (short)x1; n->maxs[1] = (short)y1; n->maxs[2] = (short)TXTMAP_SKY_HEIGHT;
			n->firstface = (unsigned short)firstface;
			n->numfaces = 1;
			return (short)node_idx;
		}
	}

	//
}
//
// 부모 인덱스가 항상 자식보다 작도록(SV_RecursiveHullCheck 규칙) 측면 충돌 체인 생성
static short build_tile_clip(bspbuilder_t* b, int c0, int r0, float pad, float z_foot)
{
	float fz = get_tile_z(b, c0, r0);
	float x0 = c0 * TXTMAP_TILE, x1 = (c0 + 1) * TXTMAP_TILE;
	float y0 = r0 * TXTMAP_TILE, y1 = (r0 + 1) * TXTMAP_TILE;

	// 바닥 충돌 노드 먼저 할당 (z < fz + z_foot 이면 SOLID)
	int floor_idx = mb_append(&b->clipnodes, NULL, sizeof(dclipnode_t)) / (int)sizeof(dclipnode_t);
	int p_floor = add_plane(b, PLANE_Z, fz + z_foot);

	// 4방향 인접 타일 중 더 높은 타일에 대해 측면 충돌(pad) 체인 연결
	struct { int nc, nr, axis; float plane_d; int solid_side; } dirs[4] = {
		{ c0 - 1, r0, PLANE_X, x0 + pad, 1 },
		{ c0 + 1, r0, PLANE_X, x1 - pad, 0 },
		{ c0, r0 - 1, PLANE_Y, y0 + pad, 1 },
		{ c0, r0 + 1, PLANE_Y, y1 - pad, 0 }
	};

	int i;
	int cur_parent = floor_idx;
	int cur_child_slot = 0; // floor_idx의 children[0](위쪽)에 체인을 이어 붙임

	((dclipnode_t*)b->clipnodes.data)[floor_idx].planenum = p_floor;
	((dclipnode_t*)b->clipnodes.data)[floor_idx].children[1] = CONTENTS_SOLID;

	for (i = 0; i < 4; i++)
	{
		float nz = get_tile_z(b, dirs[i].nc, dirs[i].nr);
		if (nz > fz)
		{
			// 높이 분기 노드: z >= nz + z_foot 이면 통과, 아래면 벽(pad) 충돌
			int z_idx = mb_append(&b->clipnodes, NULL, sizeof(dclipnode_t)) / (int)sizeof(dclipnode_t);
			int w_idx = mb_append(&b->clipnodes, NULL, sizeof(dclipnode_t)) / (int)sizeof(dclipnode_t);
			int pz = add_plane(b, PLANE_Z, nz + z_foot);
			int pw = add_plane(b, dirs[i].axis, dirs[i].plane_d);

			// 이전 부모와 연결
			((dclipnode_t*)b->clipnodes.data)[cur_parent].children[cur_child_slot] = (short)z_idx;

			((dclipnode_t*)b->clipnodes.data)[z_idx].planenum = pz;
			((dclipnode_t*)b->clipnodes.data)[z_idx].children[0] = CONTENTS_EMPTY; // 더 높으면 통과
			((dclipnode_t*)b->clipnodes.data)[z_idx].children[1] = (short)w_idx;

			((dclipnode_t*)b->clipnodes.data)[w_idx].planenum = pw;
			if (dirs[i].solid_side == 1) {
				((dclipnode_t*)b->clipnodes.data)[w_idx].children[1] = CONTENTS_SOLID;
				cur_child_slot = 0;
			}
			else {
				((dclipnode_t*)b->clipnodes.data)[w_idx].children[0] = CONTENTS_SOLID;
				cur_child_slot = 1;
			}
			cur_parent = w_idx;
		}
	}

	((dclipnode_t*)b->clipnodes.data)[cur_parent].children[cur_child_slot] = CONTENTS_EMPTY;
	return (short)floor_idx;
}

static short build_grid_clip(bspbuilder_t* b, int c0, int c1, int r0, int r1, float pad, float z_foot)
{
	if (c1 - c0 > 1)
	{
		int cm = (c0 + c1) / 2;
		int pnum = add_plane(b, PLANE_X, cm * TXTMAP_TILE);
		int idx = mb_append(&b->clipnodes, NULL, sizeof(dclipnode_t)) / (int)sizeof(dclipnode_t);
		short front = build_grid_clip(b, cm, c1, r0, r1, pad, z_foot);
		short back = build_grid_clip(b, c0, cm, r0, r1, pad, z_foot);
		dclipnode_t* cn = ((dclipnode_t*)b->clipnodes.data) + idx;
		cn->planenum = pnum;
		cn->children[0] = front;
		cn->children[1] = back;
		return (short)idx;
	}
	if (r1 - r0 > 1)
	{
		int rm = (r0 + r1) / 2;
		int pnum = add_plane(b, PLANE_Y, rm * TXTMAP_TILE);
		int idx = mb_append(&b->clipnodes, NULL, sizeof(dclipnode_t)) / (int)sizeof(dclipnode_t);
		short front = build_grid_clip(b, c0, c1, rm, r1, pad, z_foot);
		short back = build_grid_clip(b, c0, c1, r0, rm, pad, z_foot);
		dclipnode_t* cn = ((dclipnode_t*)b->clipnodes.data) + idx;
		cn->planenum = pnum;
		cn->children[0] = front;
		cn->children[1] = back;
		return (short)idx;
	}
	return build_tile_clip(b, c0, r0, pad, z_foot);
}

static short wrap_z_clip(bspbuilder_t* b, float pad, float z_foot, float zmax)
{
	int top_idx = mb_append(&b->clipnodes, NULL, sizeof(dclipnode_t)) / (int)sizeof(dclipnode_t);
	short inner = build_grid_clip(b, 0, b->cols, 0, b->rows, pad, z_foot);
	int p_top = add_plane(b, PLANE_Z, zmax);
	dclipnode_t* cn_top = ((dclipnode_t*)b->clipnodes.data) + top_idx;

	cn_top->planenum = p_top;
	cn_top->children[0] = CONTENTS_SOLID;
	cn_top->children[1] = inner;
	return (short)top_idx;
}

//
// 
static short build_grid_bsp1(bspbuilder_t* b, int c0, int c1, int r0, int r1)
{
	if (c1 - c0 > 1)
	{
		int cm = (c0 + c1) / 2;
		float x = cm * TXTMAP_TILE;
		int pnum = add_plane(b, PLANE_X, x);
		int node_idx = mb_append(&b->nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
		int firstface = b->faces.len / (int)sizeof(dface_t);
		int numfaces = 0;
		int r;

		// X 분할선 위에 놓인 수직 벽면 생성 및 이 노드에 등록
		for (r = r0; r < r1; r++)
		{
			qboolean w_left = is_wall(b, cm - 1, r);
			qboolean w_right = is_wall(b, cm, r);
			if (w_left != w_right)
			{
				float y0 = r * TXTMAP_TILE, y1 = (r + 1) * TXTMAP_TILE;
				if (w_left) // 왼쪽이 벽 -> +X 방향(side=0)
				{
					float a[3] = { x,y0,0 }, bb[3] = { x,y0,TXTMAP_HEIGHT }, c[3] = { x,y1,TXTMAP_HEIGHT }, d[3] = { x,y1,0 };
					add_quad(b, pnum, 0, 2, a, bb, c, d);
				}
				else // 오른쪽이 벽 -> -X 방향(side=1)
				{
					float a[3] = { x,y1,0 }, bb[3] = { x,y1,TXTMAP_HEIGHT }, c[3] = { x,y0,TXTMAP_HEIGHT }, d[3] = { x,y0,0 };
					add_quad(b, pnum, 1, 2, a, bb, c, d);
				}
				numfaces++;
			}
		}

		{
			short front = build_grid_bsp(b, cm, c1, r0, r1);
			short back = build_grid_bsp(b, c0, cm, r0, r1);
			dnode_t* n = ((dnode_t*)b->nodes.data) + node_idx;
			n->planenum = pnum;
			n->children[0] = front;
			n->children[1] = back;
			n->mins[0] = (short)(c0 * TXTMAP_TILE); n->mins[1] = (short)(r0 * TXTMAP_TILE); n->mins[2] = 0;
			n->maxs[0] = (short)(c1 * TXTMAP_TILE); n->maxs[1] = (short)(r1 * TXTMAP_TILE); n->maxs[2] = (short)TXTMAP_HEIGHT;
			n->firstface = (unsigned short)firstface;
			n->numfaces = (unsigned short)numfaces;
		}
		return (short)node_idx;
	}

	if (r1 - r0 > 1)
	{
		int rm = (r0 + r1) / 2;
		float y = rm * TXTMAP_TILE;
		int pnum = add_plane(b, PLANE_Y, y);
		int node_idx = mb_append(&b->nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
		int firstface = b->faces.len / (int)sizeof(dface_t);
		int numfaces = 0;
		int c;

		// Y 분할선 위에 놓인 수직 벽면 생성 및 이 노드에 등록
		for (c = c0; c < c1; c++)
		{
			qboolean w_down = is_wall(b, c, rm - 1);
			qboolean w_up = is_wall(b, c, rm);
			if (w_down != w_up)
			{
				float x0 = c * TXTMAP_TILE, x1 = (c + 1) * TXTMAP_TILE;
				if (w_down) // 아래가 벽 -> +Y 방향(side=0)
				{
					float a[3] = { x1,y,0 }, bb[3] = { x1,y,TXTMAP_HEIGHT }, c[3] = { x0,y,TXTMAP_HEIGHT }, d[3] = { x0,y,0 };
					add_quad(b, pnum, 0, 3, a, bb, c, d);
				}
				else // 위가 벽 -> -Y 방향(side=1)
				{
					float a[3] = { x0,y,0 }, bb[3] = { x0,y,TXTMAP_HEIGHT }, c[3] = { x1,y,TXTMAP_HEIGHT }, d[3] = { x1,y,0 };
					add_quad(b, pnum, 1, 3, a, bb, c, d);
				}
				numfaces++;
			}
		}

		{
			short front = build_grid_bsp(b, c0, c1, rm, r1);
			short back = build_grid_bsp(b, c0, c1, r0, rm);
			dnode_t* n = ((dnode_t*)b->nodes.data) + node_idx;
			n->planenum = pnum;
			n->children[0] = front;
			n->children[1] = back;
			n->mins[0] = (short)(c0 * TXTMAP_TILE); n->mins[1] = (short)(r0 * TXTMAP_TILE); n->mins[2] = 0;
			n->maxs[0] = (short)(c1 * TXTMAP_TILE); n->maxs[1] = (short)(r1 * TXTMAP_TILE); n->maxs[2] = (short)TXTMAP_HEIGHT;
			n->firstface = (unsigned short)firstface;
			n->numfaces = (unsigned short)numfaces;
		}
		return (short)node_idx;
	}

	// 단일 타일(c0, r0)
	if (is_wall(b, c0, r0))
		return -1; // Leaf 0 (CONTENTS_SOLID)

	{
		dleaf_t lf;
		memset(&lf, 0, sizeof(lf));
		lf.contents = CONTENTS_EMPTY;
		lf.visofs = -1;
		lf.mins[0] = (short)(c0 * TXTMAP_TILE);     lf.mins[1] = (short)(r0 * TXTMAP_TILE);     lf.mins[2] = 0;
		lf.maxs[0] = (short)((c0 + 1) * TXTMAP_TILE); lf.maxs[1] = (short)((r0 + 1) * TXTMAP_TILE); lf.maxs[2] = (short)TXTMAP_HEIGHT;
		int leaf_idx = mb_append(&b->leafs, &lf, sizeof(lf)) / (int)sizeof(dleaf_t);
		return (short)(-(leaf_idx + 1));
	}
}



// 외부에서 호출: maps/<name>.bsp 대신 maps/<name>.txt 를 찾아 메모리 BSP 생성
extern void Mod_LoadBrushModel(model_t* mod, void* buffer);

qboolean Mod_TryLoadTxtMap(model_t* mod)
{
	char txt_name[260];
	char full_path[260];
	char line[256];
	FILE* fp;
	bspbuilder_t b;
	int r, c;
	char raw_lines[TXTMAP_MAX_GRID][TXTMAP_MAX_GRID];
	int num_lines = 0, max_cols = 0;

	// "maps/xxx.bsp" -> "maps/xxx.txt" 변환
	if (strncmp(mod->name, "maps/", 5) != 0)
		return false;
	strcpy(txt_name, mod->name);
	if (strlen(txt_name) < 4 || Q_strcasecmp(txt_name + strlen(txt_name) - 4, ".bsp") != 0)
		return false;
	strcpy(txt_name + strlen(txt_name) - 4, ".txt");

	sprintf(full_path, "%s/%s", com_gamedir, txt_name);
	fp = fopen(full_path, "r");
	if (!fp)
		return false;

	while (num_lines < TXTMAP_MAX_GRID && fgets(line, sizeof(line), fp))
	{
		int len = (int)strlen(line);
		while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
			line[--len] = 0;
		if (len == 0) continue;
		if (len > TXTMAP_MAX_GRID) len = TXTMAP_MAX_GRID;
		memset(raw_lines[num_lines], '#', TXTMAP_MAX_GRID);
		memcpy(raw_lines[num_lines], line, len);
		if (len > max_cols) max_cols = len;
		num_lines++;
	}
	fclose(fp);

	if (num_lines == 0 || max_cols == 0)
		return false;

	Con_Printf("Building in-memory BSP from %s (%dx%d)...\n", txt_name, max_cols, num_lines);

	memset(&b, 0, sizeof(b));
	b.rows = num_lines;
	b.cols = max_cols;
	for (r = 0; r < num_lines; r++)
		memcpy(b.grid[num_lines - 1 - r], raw_lines[r], max_cols);

	mb_init(&b.entities, 4096);
	mb_init(&b.planes, 4096);
	mb_init(&b.textures, 32768);
	mb_init(&b.vertexes, 8192);
	mb_init(&b.nodes, 8192);
	mb_init(&b.texinfo, 1024);
	mb_init(&b.faces, 8192);
	mb_init(&b.clipnodes, 8192);
	mb_init(&b.leafs, 8192);
	mb_init(&b.marksurfaces, 4096);
	mb_init(&b.edges, 8192);
	mb_init(&b.surfedges, 8192);
	mb_init(&b.models, 512);

	// 1. Leaf 0 (CONTENTS_SOLID) 및 Edge 0 (더미) 예약
	{
		dleaf_t solid_leaf;
		dedge_t dummy_edge = { 0, 0 };
		memset(&solid_leaf, 0, sizeof(solid_leaf));
		solid_leaf.contents = CONTENTS_SOLID;
		solid_leaf.visofs = -1;
		mb_append(&b.leafs, &solid_leaf, sizeof(solid_leaf));
		mb_append(&b.edges, &dummy_edge, sizeof(dummy_edge));
	}

	// 2. 텍스처 및 엔티티 문자열 생성
	build_procedural_textures(&b);
	{
		const char* ws = "{\n\"classname\" \"worldspawn\"\n\"message\" \"In-Memory Text Map\"\n}\n";
		mb_append(&b.entities, ws, (int)strlen(ws));

		for (r = 0; r < b.rows; r++)
		{
			for (c = 0; c < b.cols; c++)
			{
				char ch = b.grid[r][c];
				int cx = (int)((c + 0.5f) * TXTMAP_TILE);
				int cy = (int)((r + 0.5f) * TXTMAP_TILE);
				char ent[256];
				ent[0] = 0;
				if (ch == 'P')
					sprintf(ent, "{\n\"classname\" \"info_player_start\"\n\"origin\" \"%d %d 32\"\n}\n", cx, cy);
				else if (ch == 'E')
					sprintf(ent, "{\n\"classname\" \"monster_army\"\n\"origin\" \"%d %d 32\"\n}\n", cx, cy);
				else if (ch == 'H')
					sprintf(ent, "{\n\"classname\" \"item_health\"\n\"origin\" \"%d %d 16\"\n}\n", cx, cy);

				if (ent[0])
					mb_append(&b.entities, ent, (int)strlen(ent));
			}
		}
		mb_append(&b.entities, "", 1); // 널 종료
	}

	/*
	// 3. 렌더링 BSP 트리 (Z=0 바닥, Z=H 천장으로 감싸기)
	{
		short inner = build_grid_bsp(&b, 0, b.cols, 0, b.rows);
		int p_top = add_plane(&b, PLANE_Z, TXTMAP_HEIGHT);
		int p_bot = add_plane(&b, PLANE_Z, 0.0f);
		int top_idx = mb_append(&b.nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
		int root_idx = mb_append(&b.nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
		dnode_t* ntop = ((dnode_t*)b.nodes.data) + top_idx;
		dnode_t* nroot = ((dnode_t*)b.nodes.data) + root_idx;

		ntop->planenum = p_top;
		ntop->children[0] = -1; // SOLID
		ntop->children[1] = inner;
		ntop->maxs[0] = (short)(b.cols * TXTMAP_TILE);
		ntop->maxs[1] = (short)(b.rows * TXTMAP_TILE);
		ntop->maxs[2] = (short)TXTMAP_HEIGHT;

		nroot->planenum = p_bot;
		nroot->children[0] = (short)top_idx;
		nroot->children[1] = -1; // SOLID
		nroot->maxs[0] = (short)(b.cols * TXTMAP_TILE);
		nroot->maxs[1] = (short)(b.rows * TXTMAP_TILE);
		nroot->maxs[2] = (short)TXTMAP_HEIGHT;

		// 4. 충돌 Hull 1 (플레이어) / Hull 2 (대형 몬스터)
		short h1 = wrap_z_clip(&b, build_grid_clip(&b, 0, b.cols, 0, b.rows, 16.0f), 24.0f, TXTMAP_HEIGHT - 32.0f);
		short h2 = wrap_z_clip(&b, build_grid_clip(&b, 0, b.cols, 0, b.rows, 32.0f), 24.0f, TXTMAP_HEIGHT - 64.0f);

		// 5. World Model (dmodel_t)
		dmodel_t dm;
		memset(&dm, 0, sizeof(dm));
		dm.maxs[0] = b.cols * TXTMAP_TILE;
		dm.maxs[1] = b.rows * TXTMAP_TILE;
		dm.maxs[2] = TXTMAP_HEIGHT;
		dm.headnode[0] = root_idx;
		dm.headnode[1] = h1;
		dm.headnode[2] = h2;
		dm.visleafs = (b.leafs.len / (int)sizeof(dleaf_t)) - 1;
		dm.firstface = 0;
		dm.numfaces = b.faces.len / (int)sizeof(dface_t);
		mb_append(&b.models, &dm, sizeof(dm));
	}
	*/

	/*
	// 3. 렌더링 BSP 트리 (바닥/천장/벽 면을 각 분할 노드에 직접 등록)
	{
		int root_idx = mb_append(&b.nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
		int top_idx = mb_append(&b.nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
		int p_bot = add_plane(&b, PLANE_Z, 0.0f);
		int p_top = add_plane(&b, PLANE_Z, TXTMAP_HEIGHT);
		int bot_first, bot_num = 0;
		int top_first, top_num = 0;
		int total_faces, i;

		// 바닥면 생성 (nroot 소속)
		bot_first = b.faces.len / (int)sizeof(dface_t);
		for (r = 0; r < b.rows; r++)
		{
			for (c = 0; c < b.cols; c++)
			{
				if (!is_wall(&b, c, r))
				{
					float x0 = c * TXTMAP_TILE, x1 = (c + 1) * TXTMAP_TILE;
					float y0 = r * TXTMAP_TILE, y1 = (r + 1) * TXTMAP_TILE;
					float a[3] = { x0,y0,0 }, bb[3] = { x0,y1,0 }, cc[3] = { x1,y1,0 }, d[3] = { x1,y0,0 };
					add_quad(&b, p_bot, 0, 0, a, bb, cc, d);
					bot_num++;
				}
			}
		}

		// 천장면 생성 (ntop 소속)
		top_first = b.faces.len / (int)sizeof(dface_t);
		for (r = 0; r < b.rows; r++)
		{
			for (c = 0; c < b.cols; c++)
			{
				if (!is_wall(&b, c, r))
				{
					float x0 = c * TXTMAP_TILE, x1 = (c + 1) * TXTMAP_TILE;
					float y0 = r * TXTMAP_TILE, y1 = (r + 1) * TXTMAP_TILE;
					float a[3] = { x0,y0,TXTMAP_HEIGHT }, bb[3] = { x1,y0,TXTMAP_HEIGHT }, cc[3] = { x1,y1,TXTMAP_HEIGHT }, d[3] = { x0,y1,TXTMAP_HEIGHT };
					add_quad(&b, p_top, 1, 1, a, bb, cc, d);
					top_num++;
				}
			}
		}

		// 내부 X/Y 벽면 및 리프 트리 생성
		{
			short inner = build_grid_bsp(&b, 0, b.cols, 0, b.rows);
			dnode_t* nroot = ((dnode_t*)b.nodes.data) + root_idx;
			dnode_t* ntop = ((dnode_t*)b.nodes.data) + top_idx;

			nroot->planenum = p_bot;
			nroot->children[0] = (short)top_idx;
			nroot->children[1] = -1;
			nroot->maxs[0] = (short)(b.cols * TXTMAP_TILE);
			nroot->maxs[1] = (short)(b.rows * TXTMAP_TILE);
			nroot->maxs[2] = (short)TXTMAP_HEIGHT;
			nroot->firstface = (unsigned short)bot_first;
			nroot->numfaces = (unsigned short)bot_num;

			ntop->planenum = p_top;
			ntop->children[0] = -1;
			ntop->children[1] = inner;
			ntop->maxs[0] = (short)(b.cols * TXTMAP_TILE);
			ntop->maxs[1] = (short)(b.rows * TXTMAP_TILE);
			ntop->maxs[2] = (short)TXTMAP_HEIGHT;
			ntop->firstface = (unsigned short)top_first;
			ntop->numfaces = (unsigned short)top_num;
		}

		// 모든 면을 marksurfaces에 등록하고 첫 번째 빈 리프(Leaf 1)에 연결
		total_faces = b.faces.len / (int)sizeof(dface_t);
		for (i = 0; i < total_faces; i++)
		{
			unsigned short fidx = (unsigned short)i;
			mb_append(&b.marksurfaces, &fidx, sizeof(unsigned short));
		}
		//
		//if (b.leafs.len / (int)sizeof(dleaf_t) > 1)
		//{
		//	dleaf_t* lf1 = ((dleaf_t*)b.leafs.data) + 1;
		//	lf1->firstmarksurface = 0;
		//	lf1->nummarksurfaces = (unsigned short)total_faces;
		//}
		

		//
		 // 변경 후: 모든 빈 리프에 marksurfaces 연결
		
			int num_leafs = b.leafs.len / (int)sizeof(dleaf_t);
			for (i = 1; i < num_leafs; i++)
			{
				dleaf_t* lf = ((dleaf_t*)b.leafs.data) + i;
				lf->firstmarksurface = 0;
				lf->nummarksurfaces = (unsigned short)total_faces;
			}	
		//
		// 4. 충돌 Hull 1 / Hull 2
		{
			short h1 = wrap_z_clip(&b, 16.0f, 24.0f, TXTMAP_HEIGHT - 32.0f);
			short h2 = wrap_z_clip(&b, 32.0f, 24.0f, TXTMAP_HEIGHT - 64.0f);

			// 5. World Model (dmodel_t)
			dmodel_t dm;
			memset(&dm, 0, sizeof(dm));
			dm.maxs[0] = b.cols * TXTMAP_TILE;
			dm.maxs[1] = b.rows * TXTMAP_TILE;
			dm.maxs[2] = TXTMAP_HEIGHT;
			dm.headnode[0] = root_idx;
			dm.headnode[1] = h1;
			dm.headnode[2] = h2;
			dm.visleafs = (b.leafs.len / (int)sizeof(dleaf_t)) - 1;
			dm.firstface = 0;
			dm.numfaces = total_faces;
			mb_append(&b.models, &dm, sizeof(dm));
		}
	}
	*/
	// 3. 렌더링 BSP 트리 (하늘 천장 + 타일별 높이 트리)
	{
		int root_idx = mb_append(&b.nodes, NULL, sizeof(dnode_t)) / (int)sizeof(dnode_t);
		int p_top = add_plane(&b, PLANE_Z, TXTMAP_SKY_HEIGHT);
		int top_first = b.faces.len / (int)sizeof(dface_t);
		int top_num = 0, total_faces, num_leafs, i;

		// 전체 하늘 천장 면 생성 (root_idx 소속)
		for (r = 0; r < b.rows; r++)
		{
			for (c = 0; c < b.cols; c++)
			{
				float x0 = c * TXTMAP_TILE, x1 = (c + 1) * TXTMAP_TILE;
				float y0 = r * TXTMAP_TILE, y1 = (r + 1) * TXTMAP_TILE;
				float a[3] = { x0,y0,TXTMAP_SKY_HEIGHT }, bb[3] = { x1,y0,TXTMAP_SKY_HEIGHT },
					cc[3] = { x1,y1,TXTMAP_SKY_HEIGHT }, d[3] = { x0,y1,TXTMAP_SKY_HEIGHT };
				add_quad(&b, p_top, 1, 1, a, bb, cc, d);
				top_num++;
			}
		}

		{
			short inner = build_grid_bsp(&b, 0, b.cols, 0, b.rows);
			dnode_t* nroot = ((dnode_t*)b.nodes.data) + root_idx;
			nroot->planenum = p_top;
			nroot->children[0] = -1;    // 하늘 위: SOLID
			nroot->children[1] = inner; // 하늘 아래: 타일 높이 트리
			nroot->mins[0] = 0; nroot->mins[1] = 0; nroot->mins[2] = (short)TXTMAP_MIN_Z;
			nroot->maxs[0] = (short)(b.cols * TXTMAP_TILE);
			nroot->maxs[1] = (short)(b.rows * TXTMAP_TILE);
			nroot->maxs[2] = (short)TXTMAP_SKY_HEIGHT;
			nroot->firstface = (unsigned short)top_first;
			nroot->numfaces = (unsigned short)top_num;
		}

		// 모든 면을 marksurfaces에 등록하고 모든 빈 리프에 연결
		total_faces = b.faces.len / (int)sizeof(dface_t);
		for (i = 0; i < total_faces; i++)
		{
			unsigned short fidx = (unsigned short)i;
			mb_append(&b.marksurfaces, &fidx, sizeof(unsigned short));
		}
		num_leafs = b.leafs.len / (int)sizeof(dleaf_t);
		for (i = 1; i < num_leafs; i++)
		{
			dleaf_t* lf = ((dleaf_t*)b.leafs.data) + i;
			lf->firstmarksurface = 0;
			lf->nummarksurfaces = (unsigned short)total_faces;
		}

		// 4. 충돌 Hull 1 / Hull 2
		{
			short h1 = wrap_z_clip(&b, 16.0f, 24.0f, TXTMAP_SKY_HEIGHT - 32.0f);
			short h2 = wrap_z_clip(&b, 32.0f, 24.0f, TXTMAP_SKY_HEIGHT - 64.0f);

			dmodel_t dm;
			memset(&dm, 0, sizeof(dm));
			dm.mins[2] = TXTMAP_MIN_Z;
			dm.maxs[0] = b.cols * TXTMAP_TILE;
			dm.maxs[1] = b.rows * TXTMAP_TILE;
			dm.maxs[2] = TXTMAP_SKY_HEIGHT;
			dm.headnode[0] = root_idx;
			dm.headnode[1] = h1;
			dm.headnode[2] = h2;
			dm.visleafs = num_leafs - 1;
			dm.firstface = 0;
			dm.numfaces = total_faces;
			mb_append(&b.models, &dm, sizeof(dm));
		}
     }


	// 6. 최종 BSP29 단일 바이너리 버퍼 조립
	{
		membuf_t out;
		dheader_t hdr;
		membuf_t* lumps[HEADER_LUMPS];
		int i;

		memset(&hdr, 0, sizeof(hdr));
		hdr.version = BSPVERSION;
		memset(lumps, 0, sizeof(lumps));
		lumps[LUMP_ENTITIES] = &b.entities;
		lumps[LUMP_PLANES] = &b.planes;
		lumps[LUMP_TEXTURES] = &b.textures;
		lumps[LUMP_VERTEXES] = &b.vertexes;
		lumps[LUMP_NODES] = &b.nodes;
		lumps[LUMP_TEXINFO] = &b.texinfo;
		lumps[LUMP_FACES] = &b.faces;
		lumps[LUMP_CLIPNODES] = &b.clipnodes;
		lumps[LUMP_LEAFS] = &b.leafs;
		lumps[LUMP_MARKSURFACES] = &b.marksurfaces;
		lumps[LUMP_EDGES] = &b.edges;
		lumps[LUMP_SURFEDGES] = &b.surfedges;
		lumps[LUMP_MODELS] = &b.models;

		mb_init(&out, 131072);
		mb_append(&out, &hdr, sizeof(hdr));

		for (i = 0; i < HEADER_LUMPS; i++)
		{
			if (lumps[i] && lumps[i]->len > 0)
			{
				// 4바이트 정렬
				while (out.len & 3) mb_append(&out, "", 1);
				hdr.lumps[i].fileofs = out.len;
				hdr.lumps[i].filelen = lumps[i]->len;
				mb_append(&out, lumps[i]->data, lumps[i]->len);
			}
		}
		memcpy(out.data, &hdr, sizeof(hdr));

		// WinQuake의 기존 BSP 로더로 즉시 로딩!
		Mod_LoadBrushModel(mod, out.data);
		mb_free(&out);
	}

	mb_free(&b.entities); mb_free(&b.planes); mb_free(&b.textures);
	mb_free(&b.vertexes); mb_free(&b.nodes);  mb_free(&b.texinfo);
	mb_free(&b.faces);    mb_free(&b.clipnodes); mb_free(&b.leafs);
	mb_free(&b.marksurfaces); mb_free(&b.edges); mb_free(&b.surfedges);
	mb_free(&b.models);

	return true;
}