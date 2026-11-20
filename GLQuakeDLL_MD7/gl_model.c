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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"

model_t	*loadmodel;
char	loadname[32];	// for hunk tags

void Mod_LoadSpriteModel (model_t *mod, void *buffer);
void Mod_LoadBrushModel (model_t *mod, void *buffer);
void Mod_LoadAliasModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel (model_t *mod, qboolean crash);

byte	mod_novis[MAX_MAP_LEAFS/8];

#define	MAX_MOD_KNOWN	512
model_t	mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

cvar_t gl_subdivide_size = {"gl_subdivide_size", "128", true};

static void GL_MakeGS_DisplayLists(aliashdr_t* hdr);
void Mod_LoadGS_MDL3(model_t* mod, void* buffer);
/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	Cvar_RegisterVariable (&gl_subdivide_size);
	memset (mod_novis, 0xff, sizeof(mod_novis));
}

void Mod_LoadGS_MDL(model_t* mod, void* buffer);
void Mod_LoadGS_MDL7(model_t* mod, void* buffer);
/*
===============
Mod_Init

Caches the data if needed
===============
*/
void *Mod_Extradata (model_t *mod)
{
	void	*r;
	
	r = Cache_Check (&mod->cache);
	if (r)
		return r;

	Mod_LoadModel (mod, true);
	
	if (!mod->cache.data)
		Sys_Error ("Mod_Extradata: caching failed");
	return mod->cache.data;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t		*node;
	float		d;
	mplane_t	*plane;
	
	if (!model || !model->nodes)
		Sys_Error ("Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}
	
	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	static byte	decompressed[MAX_MAP_LEAFS/8];
	int		c;
	byte	*out;
	int		row;

	row = (model->numleafs+7)>>3;	
	out = decompressed;

#if 0
	memcpy (out, in, row);
#else
	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return decompressed;		
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}
	
		c = in[1];
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);
#endif
	
	return decompressed;
}

byte *Mod_LeafPVS (mleaf_t *leaf, model_t *model)
{
	if (leaf == model->leafs)
		return mod_novis;
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	int		i;
	model_t	*mod;
	
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
		if (mod->type != mod_alias)
			mod->needload = true;
}

/*
==================
Mod_FindName

==================
*/
model_t *Mod_FindName (char *name)
{
	int		i;
	model_t	*mod;
	
	if (!name[0])
		Sys_Error ("Mod_ForName: NULL name");
		
//
// search the currently loaded models
//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
		if (!strcmp (mod->name, name) )
			break;
			
	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Sys_Error ("mod_numknown == MAX_MOD_KNOWN");
		strcpy (mod->name, name);
		mod->needload = true;
		mod_numknown++;
	}

	return mod;
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (char *name)
{
	model_t	*mod;
	
	mod = Mod_FindName (name);
	
	if (!mod->needload)
	{
		if (mod->type == mod_alias)
			Cache_Check (&mod->cache);
	}
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
model_t *Mod_LoadModel (model_t *mod, qboolean crash)
{
	void	*d;
	unsigned *buf;
	byte	stackbuf[1024];		// avoid dirtying the cache heap

	if (!mod->needload)
	{
		if (mod->type == mod_alias)
		{
			d = Cache_Check (&mod->cache);
			if (d)
				return mod;
		}
		else
			return mod;		// not cached at all
	}

//
// because the world is so huge, load it one piece at a time
//
	if (!crash)
	{
	
	}
	
//
// load the file
	qboolean use_malloc = false;

	buf = (unsigned *)COM_LoadStackFile (mod->name, stackbuf, sizeof(stackbuf));

	// COM_LoadStackFile이 실패하면(셰어웨어 제한 또는 외부 경로) 직접 fopen으로 읽기
	if (!buf)
	{
		char fullpath[MAX_OSPATH];
		FILE* f = NULL;

		// 1. 먼저 gamedir 기준 경로로 시도 (예: .../id1/progs/earth.mdl)
		sprintf(fullpath, "%s/%s", com_gamedir, mod->name);
		f = fopen(fullpath, "rb");

		// 2. 없으면 입력한 절대 경로 그대로 시도 (예: C:/GStudio8/samples/earth.mdl)
		if (!f)
			f = fopen(mod->name, "rb");
	
		if (f)
		{
			int len;
			fseek(f, 0, SEEK_END);
			len = ftell(f);
			fseek(f, 0, SEEK_SET);
			//buf = (unsigned*)Hunk_TempAlloc(len + 1);
			buf = (unsigned*)malloc(len + 1); // Hunk_TempAlloc 대신 malloc 사용
			fread(buf, 1, len, f);
			fclose(f);
			use_malloc = true;
		}
	}
	if (!buf)
	{
		if (crash)
			Sys_Error ("Mod_NumForName: %s not found", mod->name);
		return NULL;
	}
	
//
// allocate a new model
//
	COM_FileBase (mod->name, loadname);
	
	loadmodel = mod;

//
// fill it in
//

// call the apropriate loader
	mod->needload = false;
	
	switch (LittleLong(*(unsigned *)buf))
	{
	case IDPOLYHEADER:
		Mod_LoadAliasModel (mod, buf);
		break;
	case IDMDL3HEADER:
		Mod_LoadGS_MDL3(mod, buf);
		break;
	case IDMDL4HEADER:
	case IDMDL5HEADER:
		Mod_LoadGS_MDL(mod, buf);
		break;
	case IDMDL7HEADER:
		Mod_LoadGS_MDL7(mod, buf);
		break;
	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;
	
	default:
		Mod_LoadBrushModel (mod, buf);
		break;
	}

	if (use_malloc)
		free(buf);

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, qboolean crash)
{
	model_t	*mod;
	
	mod = Mod_FindName (name);
	
	return Mod_LoadModel (mod, crash);
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte	*mod_base;


/*
=================
Mod_LoadTextures
=================
*/
void Mod_LoadTextures (lump_t *l)
{
	int		i, j, pixels, num, max, altmax;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t *m;

	if (!l->filelen)
	{
		loadmodel->textures = NULL;
		return;
	}
	m = (dmiptexlump_t *)(mod_base + l->fileofs);
	
	m->nummiptex = LittleLong (m->nummiptex);
	
	loadmodel->numtextures = m->nummiptex;
	loadmodel->textures = Hunk_AllocName (m->nummiptex * sizeof(*loadmodel->textures) , loadname);

	for (i=0 ; i<m->nummiptex ; i++)
	{
		m->dataofs[i] = LittleLong(m->dataofs[i]);
		if (m->dataofs[i] == -1)
			continue;
		mt = (miptex_t *)((byte *)m + m->dataofs[i]);
		mt->width = LittleLong (mt->width);
		mt->height = LittleLong (mt->height);
		for (j=0 ; j<MIPLEVELS ; j++)
			mt->offsets[j] = LittleLong (mt->offsets[j]);
		
		if ( (mt->width & 15) || (mt->height & 15) )
			Sys_Error ("Texture %s is not 16 aligned", mt->name);
		pixels = mt->width*mt->height/64*85;
		tx = Hunk_AllocName (sizeof(texture_t) +pixels, loadname );
		loadmodel->textures[i] = tx;

		memcpy (tx->name, mt->name, sizeof(tx->name));
		tx->width = mt->width;
		tx->height = mt->height;
		for (j=0 ; j<MIPLEVELS ; j++)
			tx->offsets[j] = mt->offsets[j] + sizeof(texture_t) - sizeof(miptex_t);
		// the pixels immediately follow the structures
		memcpy ( tx+1, mt+1, pixels);
		

		if (!Q_strncmp(mt->name,"sky",3))	
			R_InitSky (tx);
		else
		{
			texture_mode = GL_LINEAR_MIPMAP_NEAREST; //_LINEAR;
			tx->gl_texturenum = GL_LoadTexture (mt->name, tx->width, tx->height, (byte *)(tx+1), true, false);
			texture_mode = GL_LINEAR;
		}
	}

//
// sequence the animations
//
	for (i=0 ; i<m->nummiptex ; i++)
	{
		tx = loadmodel->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue;	// allready sequenced

	// find the number of frames in the animation
		memset (anims, 0, sizeof(anims));
		memset (altanims, 0, sizeof(altanims));

		max = tx->name[1];
		altmax = 0;
		if (max >= 'a' && max <= 'z')
			max -= 'a' - 'A';
		if (max >= '0' && max <= '9')
		{
			max -= '0';
			altmax = 0;
			anims[max] = tx;
			max++;
		}
		else if (max >= 'A' && max <= 'J')
		{
			altmax = max - 'A';
			max = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else
			Sys_Error ("Bad animating texture %s", tx->name);

		for (j=i+1 ; j<m->nummiptex ; j++)
		{
			tx2 = loadmodel->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name+2, tx->name+2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num+1 > max)
					max = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num+1 > altmax)
					altmax = num+1;
			}
			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}
		
#define	ANIM_CYCLE	2
	// link them all together
		for (j=0 ; j<max ; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = max * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = anims[ (j+1)%max ];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j=0 ; j<altmax ; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[ (j+1)%altmax ];
			if (max)
				tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_LoadLighting
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		return;
	}
	loadmodel->lightdata = Hunk_AllocName ( l->filelen, loadname);	
	memcpy (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->visdata = NULL;
		return;
	}
	loadmodel->visdata = Hunk_AllocName ( l->filelen, loadname);	
	memcpy (loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadEntities
=================
*/
void Mod_LoadEntities (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->entities = NULL;
		return;
	}
	loadmodel->entities = Hunk_AllocName ( l->filelen, loadname);	
	memcpy (loadmodel->entities, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	dmodel_t	*out;
	int			i, j, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		for (j=0 ; j<MAX_MAP_HULLS ; j++)
			out->headnode[j] = LittleLong (in->headnode[j]);
		out->visleafs = LittleLong (in->visleafs);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l)
{
	dedge_t *in;
	medge_t *out;
	int 	i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( (count + 1) * sizeof(*out), loadname);	

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out;
	int 	i, j, count;
	int		miptex;
	float	len1, len2;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<8 ; j++)
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
		len1 = Length (out->vecs[0]);
		len2 = Length (out->vecs[1]);
		len1 = (len1 + len2)/2;
		if (len1 < 0.32)
			out->mipadjust = 4;
		else if (len1 < 0.49)
			out->mipadjust = 3;
		else if (len1 < 0.99)
			out->mipadjust = 2;
		else
			out->mipadjust = 1;
#if 0
		if (len1 + len2 < 0.001)
			out->mipadjust = 1;		// don't crash
		else
			out->mipadjust = 1 / floor( (len1+len2)/2 + 0.1 );
#endif

		miptex = LittleLong (in->miptex);
		out->flags = LittleLong (in->flags);
	
		if (!loadmodel->textures)
		{
			out->texture = r_notexture_mip;	// checkerboard texture
			out->flags = 0;
		}
		else
		{
			if (miptex >= loadmodel->numtextures)
				Sys_Error ("miptex >= loadmodel->numtextures");
			out->texture = loadmodel->textures[miptex];
			if (!out->texture)
			{
				out->texture = r_notexture_mip; // texture not found
				out->flags = 0;
			}
		}
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;
	
	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		
		for (j=0 ; j<2 ; j++)
		{
			val = v->position[0] * tex->vecs[j][0] + 
				v->position[1] * tex->vecs[j][1] +
				v->position[2] * tex->vecs[j][2] +
				tex->vecs[j][3];
			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{	
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;
		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 512 /* 256 */ )
			Sys_Error ("Bad surface extents");
	}
}


/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l)
{
	dface_t		*in;
	msurface_t 	*out;
	int			i, count, surfnum;
	int			planenum, side;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	for ( surfnum=0 ; surfnum<count ; surfnum++, in++, out++)
	{
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);		
		out->flags = 0;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);
		if (side)
			out->flags |= SURF_PLANEBACK;			

		out->plane = loadmodel->planes + planenum;

		out->texinfo = loadmodel->texinfo + LittleShort (in->texinfo);

		CalcSurfaceExtents (out);
				
	// lighting info

		for (i=0 ; i<MAXLIGHTMAPS ; i++)
			out->styles[i] = in->styles[i];
		i = LittleLong(in->lightofs);
		if (i == -1)
			out->samples = NULL;
		else
			out->samples = loadmodel->lightdata + i;
		
	// set the drawing flags flag
		
		if (!Q_strncmp(out->texinfo->texture->name,"sky",3))	// sky
		{
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
#ifndef QUAKE2
			GL_SubdivideSurface (out);	// cut up polygon for warps
#endif
			continue;
		}
		
		if (!Q_strncmp(out->texinfo->texture->name,"*",1))		// turbulent
		{
			out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);
			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
			GL_SubdivideSurface (out);	// cut up polygon for warps
			continue;
		}

	}
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents < 0)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p;
	dnode_t		*in;
	mnode_t 	*out;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}
	
		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleShort (in->firstface);
		out->numsurfaces = LittleShort (in->numfaces);
		
		for (j=0 ; j<2 ; j++)
		{
			p = LittleShort (in->children[j]);
			if (p >= 0)
				out->children[j] = loadmodel->nodes + p;
			else
				out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
		}
	}
	
	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	dleaf_t 	*in;
	mleaf_t 	*out;
	int			i, j, count, p;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces +
			LittleShort(in->firstmarksurface);
		out->nummarksurfaces = LittleShort(in->nummarksurfaces);
		
		p = LittleLong(in->visofs);
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = loadmodel->visdata + p;
		out->efrags = NULL;
		
		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		// gl underwater warp
		if (out->contents != CONTENTS_EMPTY)
		{
			for (j=0 ; j<out->nummarksurfaces ; j++)
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
		}
	}	
}

/*
=================
Mod_LoadClipnodes
=================
*/
void Mod_LoadClipnodes (lump_t *l)
{
	dclipnode_t *in, *out;
	int			i, count;
	hull_t		*hull;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count;

	hull = &loadmodel->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	hull = &loadmodel->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -32;
	hull->clip_mins[1] = -32;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 32;
	hull->clip_maxs[1] = 32;
	hull->clip_maxs[2] = 64;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = LittleLong(in->planenum);
		out->children[0] = LittleShort(in->children[0]);
		out->children[1] = LittleShort(in->children[1]);
	}
}

/*
=================
Mod_MakeHull0

Deplicate the drawing hull structure as a clipping hull
=================
*/
void Mod_MakeHull0 (void)
{
	mnode_t		*in, *child;
	dclipnode_t *out;
	int			i, j, count;
	hull_t		*hull;
	
	hull = &loadmodel->hulls[0];	
	
	in = loadmodel->nodes;
	count = loadmodel->numnodes;
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = in->plane - loadmodel->planes;
		for (j=0 ; j<2 ; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - loadmodel->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{	
	int		i, j, count;
	short		*in;
	msurface_t **out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		j = LittleShort(in[i]);
		if (j >= loadmodel->numsurfaces)
			Sys_Error ("Mod_ParseMarksurfaces: bad surface number");
		out[i] = loadmodel->surfaces + j;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{	
	int		i, count;
	int		*in, *out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*2*sizeof(*out), loadname);	
	
	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return Length (corner);
}

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	int			i, j;
	dheader_t	*header;
	dmodel_t 	*bm;
	
	loadmodel->type = mod_brush;
	
	header = (dheader_t *)buffer;

	i = LittleLong (header->version);
	if (i != BSPVERSION)
		Sys_Error ("Mod_LoadBrushModel: %s has wrong version number (%i should be %i)", mod->name, i, BSPVERSION);

// swap all the lumps
	mod_base = (byte *)header;

	for (i=0 ; i<sizeof(dheader_t)/4 ; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

// load into heap
	
	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadTextures (&header->lumps[LUMP_TEXTURES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_MARKSURFACES]);
	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes (&header->lumps[LUMP_NODES]);
	Mod_LoadClipnodes (&header->lumps[LUMP_CLIPNODES]);
	Mod_LoadEntities (&header->lumps[LUMP_ENTITIES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);

	Mod_MakeHull0 ();
	
	mod->numframes = 2;		// regular and alternate animation
	
//
// set up the submodels (FIXME: this is confusing)
//
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		bm = &mod->submodels[i];

		mod->hulls[0].firstclipnode = bm->headnode[0];
		for (j=1 ; j<MAX_MAP_HULLS ; j++)
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
			mod->hulls[j].lastclipnode = mod->numclipnodes-1;
		}
		
		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;
		
		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);

		mod->numleafs = bm->visleafs;

		if (i < mod->numsubmodels-1)
		{	// duplicate the basic information
			char	name[10];

			sprintf (name, "*%i", i+1);
			loadmodel = Mod_FindName (name);
			*loadmodel = *mod;
			strcpy (loadmodel->name, name);
			mod = loadmodel;
		}
	}
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

aliashdr_t	*pheader;

stvert_t	stverts[MAXALIASVERTS];
mtriangle_t	triangles[MAXALIASTRIS];

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
trivertx_t	*poseverts[MAXALIASFRAMES];
int			posenum;

byte		**player_8bit_texels_tbl;
byte		*player_8bit_texels;

/*
=================
Mod_LoadAliasFrame
=================
*/
void * Mod_LoadAliasFrame (void * pin, maliasframedesc_t *frame)
{
	trivertx_t		*pframe, *pinframe;
	int				i, j;
	daliasframe_t	*pdaliasframe;
	
	pdaliasframe = (daliasframe_t *)pin;

	strcpy (frame->name, pdaliasframe->name);
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i=0 ; i<3 ; i++)
	{
	// these are byte values, so we don't have to worry about
	// endianness
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmin.v[i] = pdaliasframe->bboxmax.v[i];
	}

	pinframe = (trivertx_t *)(pdaliasframe + 1);

	poseverts[posenum] = pinframe;
	posenum++;

	pinframe += pheader->numverts;

	return (void *)pinframe;
}


/*
=================
Mod_LoadAliasGroup
=================
*/
void *Mod_LoadAliasGroup (void * pin,  maliasframedesc_t *frame)
{
	daliasgroup_t		*pingroup;
	int					i, numframes;
	daliasinterval_t	*pin_intervals;
	void				*ptemp;
	
	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i=0 ; i<3 ; i++)
	{
	// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmin.v[i] = pingroup->bboxmax.v[i];
	}

	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		poseverts[posenum] = (trivertx_t *)((daliasframe_t *)ptemp + 1);
		posenum++;

		ptemp = (trivertx_t *)((daliasframe_t *)ptemp + 1) + pheader->numverts;
	}

	return ptemp;
}

//=========================================================

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct
{
	short		x, y;
} floodfill_t;

extern unsigned d_8to24table[];

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

void Mod_FloodFillSkin( byte *skin, int skinwidth, int skinheight )
{
	byte				fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t			fifo[FLOODFILL_FIFO_SIZE];
	int					inpt = 0, outpt = 0;
	int					filledcolor = -1;
	int					i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP( -1, -1, 0 );
		if (x < skinwidth - 1)	FLOODFILL_STEP( 1, 1, 0 );
		if (y > 0)				FLOODFILL_STEP( -skinwidth, 0, -1 );
		if (y < skinheight - 1)	FLOODFILL_STEP( skinwidth, 0, 1 );
		skin[x + skinwidth * y] = fdc;
	}
}

/*
===============
Mod_LoadAllSkins
===============
*/
/*
void *Mod_LoadAllSkins (int numskins, daliasskintype_t *pskintype)
{
	int		i, j, k;
	char	name[32];
	int		s;
	byte	*copy;
	byte	*skin;
	byte	*texels;
	daliasskingroup_t		*pinskingroup;
	int		groupskins;
	daliasskininterval_t	*pinskinintervals;
	
	skin = (byte *)(pskintype + 1);

	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of skins: %d\n", numskins);

	s = pheader->skinwidth * pheader->skinheight;

	for (i=0 ; i<numskins ; i++)
	{
		if (pskintype->type == ALIAS_SKIN_SINGLE) {
			Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );

			// save 8 bit texels for the player model to remap
	//		if (!strcmp(loadmodel->name,"progs/player.mdl")) {
				texels = Hunk_AllocName(s, loadname);
				pheader->texels[i] = texels - (byte *)pheader;
				memcpy (texels, (byte *)(pskintype + 1), s);
	//		}
			sprintf (name, "%s_%i", loadmodel->name, i);
			pheader->gl_texturenum[i][0] =
			pheader->gl_texturenum[i][1] =
			pheader->gl_texturenum[i][2] =
			pheader->gl_texturenum[i][3] =
				GL_LoadTexture (name, pheader->skinwidth, 
				pheader->skinheight, (byte *)(pskintype + 1), true, false);
			pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);
		} else {
			// animating skin group.  yuck.
			pskintype++;
			pinskingroup = (daliasskingroup_t *)pskintype;
			groupskins = LittleLong (pinskingroup->numskins);
			pinskinintervals = (daliasskininterval_t *)(pinskingroup + 1);

			pskintype = (void *)(pinskinintervals + groupskins);

			for (j=0 ; j<groupskins ; j++)
			{
					Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );
					if (j == 0) {
						texels = Hunk_AllocName(s, loadname);
						pheader->texels[i] = texels - (byte *)pheader;
						memcpy (texels, (byte *)(pskintype), s);
					}
					sprintf (name, "%s_%i_%i", loadmodel->name, i,j);
					pheader->gl_texturenum[i][j&3] = 
						GL_LoadTexture (name, pheader->skinwidth, 
						pheader->skinheight, (byte *)(pskintype), true, false);
					pskintype = (daliasskintype_t *)((byte *)(pskintype) + s);
			}
			k = j;
			for(; j < 4; j++)
				pheader->gl_texturenum[i][j&3] = 
				pheader->gl_texturenum[i][j - k]; 
		}
	}

	return (void *)pskintype;
}
*/
//
// 
void* Mod_LoadAllSkins(int numskins, daliasskintype_t* pskintype)
{
	int		i, j, k;
	char	name[32];
	int		s;
	byte* copy;
	byte* skin;
	byte* texels;
	daliasskingroup_t* pinskingroup;
	int		groupskins;
	daliasskininterval_t* pinskinintervals;

	skin = (byte*)(pskintype + 1);

	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error("Mod_LoadAliasModel: Invalid # of skins: %d\n", numskins);

	s = pheader->skinwidth * pheader->skinheight;

	for (i = 0; i < numskins; i++)
	{
		groupskins = LittleLong(pskintype->type);

		// [추가] GameStudio IDPO 트루컬러 스킨(16/24/32비트) 처리
		if (groupskins >= 2)
		{
			int format = groupskins & 0x0F;
			int bpp = (format == 2 || format == 3 || format == 10 || format == 11) ? 2 :
				(format == 4 || format == 12) ? 3 :
				(format == 5 || format == 13) ? 4 : 1;
			int imgsize = pheader->skinwidth * pheader->skinheight * bpp;
			int has_mip = (format >= 10 && format <= 13);
			byte* skindata = (byte*)(pskintype + 1);

			pheader->gl_texturenum[i][0] =
				pheader->gl_texturenum[i][1] =
				pheader->gl_texturenum[i][2] =
				pheader->gl_texturenum[i][3] = GL_UploadGS_Skin(skindata, pheader->skinwidth, pheader->skinheight, groupskins);

			if (has_mip)
				imgsize += (imgsize / 4) + (imgsize / 16) + (imgsize / 64);

			pskintype = (daliasskintype_t*)(skindata + imgsize);
			continue;
		}

		if (pskintype->type == ALIAS_SKIN_SINGLE) {
			Mod_FloodFillSkin(skin, pheader->skinwidth, pheader->skinheight);

			// save 8 bit texels for the player model to remap
	//		if (!strcmp(loadmodel->name,"progs/player.mdl")) {
			texels = Hunk_AllocName(s, loadname);
			pheader->texels[i] = texels - (byte*)pheader;
			memcpy(texels, (byte*)(pskintype + 1), s);
			//		}
			sprintf(name, "%s_%i", loadmodel->name, i);
			pheader->gl_texturenum[i][0] =
				pheader->gl_texturenum[i][1] =
				pheader->gl_texturenum[i][2] =
				pheader->gl_texturenum[i][3] =
				GL_LoadTexture(name, pheader->skinwidth,
					pheader->skinheight, (byte*)(pskintype + 1), true, false);
			pskintype = (daliasskintype_t*)((byte*)(pskintype + 1) + s);
		}
		else {
			// animating skin group.  yuck.
			pskintype++;
			pinskingroup = (daliasskingroup_t*)pskintype;
			groupskins = LittleLong(pinskingroup->numskins);
			pinskinintervals = (daliasskininterval_t*)(pinskingroup + 1);

			pskintype = (void*)(pinskinintervals + groupskins);

			for (j = 0; j < groupskins; j++)
			{
				Mod_FloodFillSkin(skin, pheader->skinwidth, pheader->skinheight);
				if (j == 0) {
					texels = Hunk_AllocName(s, loadname);
					pheader->texels[i] = texels - (byte*)pheader;
					memcpy(texels, (byte*)(pskintype), s);
				}
				sprintf(name, "%s_%i_%i", loadmodel->name, i, j);
				pheader->gl_texturenum[i][j & 3] =
					GL_LoadTexture(name, pheader->skinwidth,
						pheader->skinheight, (byte*)(pskintype), true, false);
				pskintype = (daliasskintype_t*)((byte*)(pskintype)+s);
			}
			k = j;
			for (/* */; j < 4; j++)
		    	pheader->gl_texturenum[i][j & 3] =
			     pheader->gl_texturenum[i][j - k];
		}
	}

	return (void*)pskintype;
}


//
// 
// gl_model.c 상단 또는 Mod_LoadAliasModel 위쪽에 추가

extern int texture_extension_number; // gl_draw.c에 정의된 텍스처 ID 카운터

static int GL_UploadGS_Skin(byte* data, int width, int height, int skintype)
{
	int i, pixels;
	unsigned int* rgba;
	int texnum;
	int format = skintype & 0x0F;

	if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
	{
		Con_Printf("GL_UploadGS_Skin: Invalid size %dx%d (type 0x%02X)\n", width, height, skintype);
		return 0;
	}

	pixels = width * height;
	rgba = (unsigned int*)malloc(pixels * 4);
	if (!rgba)
		Sys_Error("GL_UploadGS_Skin: out of memory");

	if (format == 2 || format == 10) // 16-bit RGB565
	{
		unsigned short* src = (unsigned short*)data;
		for (i = 0; i < pixels; i++)
		{
			unsigned short c = src[i];
			byte r = ((c >> 11) & 0x1F) << 3;
			byte g = ((c >> 5) & 0x3F) << 2;
			byte b = (c & 0x1F) << 3;
			rgba[i] = (255 << 24) | (b << 16) | (g << 8) | r;
		}
	}
	else if (format == 4 || format == 12) // 24-bit BGR -> RGBA
	{
		for (i = 0; i < pixels; i++)
		{
			byte b = data[i * 3 + 0];
			byte g = data[i * 3 + 1];
			byte r = data[i * 3 + 2];
			rgba[i] = (255 << 24) | (b << 16) | (g << 8) | r;
		}
	}
	else if (format == 5 || format == 13) // 32-bit BGRA -> RGBA
	{
		for (i = 0; i < pixels; i++)
		{
			byte b = data[i * 4 + 0];
			byte g = data[i * 4 + 1];
			byte r = data[i * 4 + 2];
			byte a = data[i * 4 + 3];
			rgba[i] = (a << 24) | (b << 16) | (g << 8) | r;
		}
	}
	else
	{
		for (i = 0; i < pixels; i++)
			rgba[i] = d_8to24table[data[i]];
	}

	texnum = texture_extension_number++;
	GL_Bind(texnum);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	free(rgba);
	return texnum;
}



//
//=========================================================================

/*
=================
Mod_LoadAliasModel
=================
*/
// gl_model.c 에 추가
void Mod_LoadGS_MDL3(model_t* mod, void* buffer)
{
	int i, j, f, start, end, total, header_size;
	mdl3_header_t* hdr = (mdl3_header_t*)buffer;
	byte* ptr;
	mdl3_uvvert_t* uvs;
	mdl3_triangle_t* tris;
	int numskins, numverts, numtris, numframes, numskinverts;
	int* cmds, * p;
	trivertx_t* verts, * vout;

	start = Hunk_LowMark();
	ptr = (byte*)(hdr + 1); // 84바이트 헤더 이후

	numskins = hdr->numskins;
	numverts = hdr->numverts;
	numtris = hdr->numtris;
	numframes = hdr->numframes;
	numskinverts = hdr->numskinverts;

	if (numframes < 1) numframes = 1;
	if (numframes > MAXALIASFRAMES) numframes = MAXALIASFRAMES;
	if (numverts > MAXALIASVERTS || numtris > MAXALIASTRIS)
		Sys_Error("Mod_LoadGS_MDL3: %s exceeds limits (v:%d, t:%d)", mod->name, numverts, numtris);

	header_size = sizeof(aliashdr_t) + (numframes - 1) * sizeof(maliasframedesc_t);
	pheader = Hunk_AllocName(header_size, loadname);
	mod->type = mod_alias;
	mod->numframes = numframes;

	pheader->numskins = (numskins > 0) ? numskins : 1;
	pheader->skinwidth = hdr->skinwidth;
	pheader->skinheight = hdr->skinheight;
	pheader->numverts = numverts;
	pheader->numtris = numtris;
	pheader->numframes = numframes;
	pheader->numposes = numframes;

	for (j = 0; j < 3; j++)
	{
		pheader->scale[j] = hdr->scale[j];
		pheader->scale_origin[j] = hdr->scale_origin[j];
	}

	// 1. Skins 읽기 (4바이트 type + 픽셀 데이터)
	for (i = 0; i < numskins; i++)
	{
		int skintype = *(int*)ptr;
		int format = skintype & 0x0F;
		int bpp = (format == 2 || format == 3 || format == 10 || format == 11) ? 2 :
			(format == 4 || format == 12) ? 3 :
			(format == 5 || format == 13) ? 4 : 1;
		int has_mip = (format >= 10 && format <= 13);
		int imgsize = hdr->skinwidth * hdr->skinheight * bpp;

		ptr += 4;
		if (i < MAX_SKINS)
		{
			pheader->gl_texturenum[i][0] =
				pheader->gl_texturenum[i][1] =
				pheader->gl_texturenum[i][2] =
				pheader->gl_texturenum[i][3] = GL_UploadGS_Skin(ptr, hdr->skinwidth, hdr->skinheight, skintype);
		}

		if (has_mip)
			imgsize += (imgsize / 4) + (imgsize / 16) + (imgsize / 64);
		ptr += imgsize;
	}

	// 2. UV(Skinverts) 및 Triangles 포인터 저장
	uvs = (mdl3_uvvert_t*)ptr;
	ptr += numskinverts * sizeof(mdl3_uvvert_t);

	tris = (mdl3_triangle_t*)ptr;
	ptr += numtris * sizeof(mdl3_triangle_t);

	// 3. Frames 읽기 (Quake 1과 동일한 4바이트 type + 24바이트 헤더 + 정점 배열)
	for (f = 0; f < numframes; f++)
	{
		daliasframe_t* dframe;
		trivertx_t* qverts;

		ptr += 4; // frametype (0 = single)
		dframe = (daliasframe_t*)ptr;
		ptr += sizeof(daliasframe_t);

		qverts = Hunk_AllocName(numverts * sizeof(trivertx_t), loadname);
		memcpy(qverts, ptr, numverts * sizeof(trivertx_t));
		ptr += numverts * sizeof(trivertx_t);

		pheader->frames[f].firstpose = f;
		pheader->frames[f].numposes = 1;
		pheader->frames[f].interval = 0.1f;
		memcpy(pheader->frames[f].name, dframe->name, 16);
		poseverts[f] = qverts;
	}

	// 4. OpenGL 드로우 명령 및 전체 프레임 정점 버퍼 생성
	cmds = (int*)Hunk_Alloc((numtris * 7 + 1) * sizeof(int));
	pheader->commands = (byte*)cmds - (byte*)pheader;

	p = cmds;
	for (i = 0; i < numtris; i++)
	{
		*p++ = 3;
		for (j = 0; j < 3; j++)
		{
			int uvidx = tris[i].index_uv[j];
			float s = (uvs[uvidx].u + 0.5f) / (float)hdr->skinwidth;
			float t = (uvs[uvidx].v + 0.5f) / (float)hdr->skinheight;
			*(float*)p++ = s;
			*(float*)p++ = t;
		}
	}
	*p++ = 0;

	pheader->poseverts = numtris * 3;
	verts = (trivertx_t*)Hunk_Alloc(numframes * pheader->poseverts * sizeof(trivertx_t));
	pheader->posedata = (byte*)verts - (byte*)pheader;

	vout = verts;
	for (f = 0; f < numframes; f++)
	{
		for (i = 0; i < numtris; i++)
		{
			for (j = 0; j < 3; j++)
			{
				int vidx = tris[i].index_xyz[j];
				*vout++ = poseverts[f][vidx];
			}
		}
	}

	for (j = 0; j < 3; j++)
	{
		mod->mins[j] = hdr->scale_origin[j];
		mod->maxs[j] = hdr->scale_origin[j] + 255.0f * hdr->scale[j];
	}

	Con_Printf("MDL3 Loaded: %s (v:%d, t:%d, f:%d)\n", mod->name, numverts, numtris, numframes);

	end = Hunk_LowMark();
	total = end - start;
	Cache_Alloc(&mod->cache, total, loadname);
	if (!mod->cache.data) return;
	memcpy(mod->cache.data, pheader, total);
	Hunk_FreeToLowMark(start);
}

void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	int					i, j;
	mdl_t				*pinmodel;
	stvert_t			*pinstverts;
	dtriangle_t			*pintriangles;
	int					version, numframes, numskins;
	int					size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;
	int					start, end, total;
	
	start = Hunk_LowMark ();

	pinmodel = (mdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	size = 	sizeof (aliashdr_t) 
			+ (LittleLong (pinmodel->numframes) - 1) *
			sizeof (pheader->frames[0]);
	pheader = Hunk_AllocName (size, loadname);
	
	mod->flags = LittleLong (pinmodel->flags);

//
// endian-adjust and copy the data, starting with the alias model header
//
	pheader->boundingradius = LittleFloat (pinmodel->boundingradius);
	pheader->numskins = LittleLong (pinmodel->numskins);
	pheader->skinwidth = LittleLong (pinmodel->skinwidth);
	pheader->skinheight = LittleLong (pinmodel->skinheight);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		Sys_Error ("model %s has a skin taller than %d", mod->name,
				   MAX_LBM_HEIGHT);

	pheader->numverts = LittleLong (pinmodel->numverts);

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices", mod->name);

	pheader->numtris = LittleLong (pinmodel->numtris);

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	pheader->numframes = LittleLong (pinmodel->numframes);
	numframes = pheader->numframes;
	if (numframes < 1)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of frames: %d\n", numframes);

	pheader->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = LittleLong (pinmodel->synctype);
	mod->numframes = pheader->numframes;

	for (i=0 ; i<3 ; i++)
	{
		pheader->scale[i] = LittleFloat (pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
		pheader->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]);
	}


//
// load the skins
//
	pskintype = (daliasskintype_t *)&pinmodel[1];
	pskintype = Mod_LoadAllSkins (pheader->numskins, pskintype);

//
// load base s and t vertices
//
	pinstverts = (stvert_t *)pskintype;

	for (i=0 ; i<pheader->numverts ; i++)
	{
		stverts[i].onseam = LittleLong (pinstverts[i].onseam);
		stverts[i].s = LittleLong (pinstverts[i].s);
		stverts[i].t = LittleLong (pinstverts[i].t);
	}

//
// load triangle lists
//
	pintriangles = (dtriangle_t *)&pinstverts[pheader->numverts];

	for (i=0 ; i<pheader->numtris ; i++)
	{
		triangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j=0 ; j<3 ; j++)
		{
			triangles[i].vertindex[j] =
					LittleLong (pintriangles[i].vertindex[j]);
		}
	}

//
// load the frames
//
	posenum = 0;
	pframetype = (daliasframetype_t *)&pintriangles[pheader->numtris];

	for (i=0 ; i<numframes ; i++)
	{
		aliasframetype_t	frametype;

		frametype = LittleLong (pframetype->type);

		if (frametype == ALIAS_SINGLE)
		{
			pframetype = (daliasframetype_t *)
					Mod_LoadAliasFrame (pframetype + 1, &pheader->frames[i]);
		}
		else
		{
			pframetype = (daliasframetype_t *)
					Mod_LoadAliasGroup (pframetype + 1, &pheader->frames[i]);
		}
	}

	pheader->numposes = posenum;

	mod->type = mod_alias;

// FIXME: do this right
	mod->mins[0] = mod->mins[1] = mod->mins[2] = -16;
	mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 16;

	//
	// build the draw lists
	//
	//GL_MakeAliasModelDisplayLists (mod, pheader);
	// gl_model.c -> Mod_LoadAliasModel() 내부
	if (pheader->numtris > 1000)
		GL_MakeGS_DisplayLists(pheader);
	else
		GL_MakeAliasModelDisplayLists(mod, pheader);
//
// move the complete, relocatable alias model to the cache
//	
	end = Hunk_LowMark ();
	total = end - start;
	
	Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
		return;
	memcpy (mod->cache.data, pheader, total);

	Hunk_FreeToLowMark (start);
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
void * Mod_LoadSpriteFrame (void * pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					i, width, height, size, origin[2];
	unsigned short		*ppixout;
	byte				*ppixin;
	char				name[64];

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;

	pspriteframe = Hunk_AllocName (sizeof (mspriteframe_t),loadname);

	Q_memset (pspriteframe, 0, sizeof (mspriteframe_t));

	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	sprintf (name, "%s_%i", loadmodel->name, framenum);
	pspriteframe->gl_texturenum = GL_LoadTexture (name, width, height, (byte *)(pinframe + 1), true, true);

	return (void *)((byte *)pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
void * Mod_LoadSpriteGroup (void * pin, mspriteframe_t **ppframe, int framenum)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	float				*poutintervals;
	void				*ptemp;

	pingroup = (dspritegroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	pspritegroup = Hunk_AllocName (sizeof (mspritegroup_t) +
				(numframes - 1) * sizeof (pspritegroup->frames[0]), loadname);

	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	poutintervals = Hunk_AllocName (numframes * sizeof (float), loadname);

	pspritegroup->intervals = poutintervals;

	for (i=0 ; i<numframes ; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
			Sys_Error ("Mod_LoadSpriteGroup: interval<=0");

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		ptemp = Mod_LoadSpriteFrame (ptemp, &pspritegroup->frames[i], framenum * 100 + i);
	}

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	int					i;
	int					version;
	dsprite_t			*pin;
	msprite_t			*psprite;
	int					numframes;
	int					size;
	dspriteframetype_t	*pframetype;
	
	pin = (dsprite_t *)buffer;

	version = LittleLong (pin->version);
	if (version != SPRITE_VERSION)
		Sys_Error ("%s has wrong version number "
				 "(%i should be %i)", mod->name, version, SPRITE_VERSION);

	numframes = LittleLong (pin->numframes);

	size = sizeof (msprite_t) +	(numframes - 1) * sizeof (psprite->frames);

	psprite = Hunk_AllocName (size, loadname);

	mod->cache.data = psprite;

	psprite->type = LittleLong (pin->type);
	psprite->maxwidth = LittleLong (pin->width);
	psprite->maxheight = LittleLong (pin->height);
	psprite->beamlength = LittleFloat (pin->beamlength);
	mod->synctype = LittleLong (pin->synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;
	
//
// load the frames
//
	if (numframes < 1)
		Sys_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);

	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *)(pin + 1);

	for (i=0 ; i<numframes ; i++)
	{
		spriteframetype_t	frametype;

		frametype = LittleLong (pframetype->type);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteFrame (pframetype + 1,
										 &psprite->frames[i].frameptr, i);
		}
		else
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteGroup (pframetype + 1,
										 &psprite->frames[i].frameptr, i);
		}
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
void Mod_Print (void)
{
	int		i;
	model_t	*mod;

	Con_Printf ("Cached models:\n");
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		Con_Printf ("%8p : %s\n",mod->cache.data, mod->name);
	}
}

// gl_model.c -> GL_MakeGS_DisplayLists() 교체
static void GL_MakeGS_DisplayLists(aliashdr_t* hdr)
{
	int i, j, f;
	int numtris = hdr->numtris;
	int numposes = hdr->numposes;
	int numcmds = numtris * 7 + 1;
	int* cmds;
	int* p;
	trivertx_t* verts;
	trivertx_t* vout;

	// 1. OpenGL 드로우 명령 버퍼 (모든 프레임이 동일한 UV/삼각형 구조 공유)
	cmds = (int*)Hunk_Alloc(numcmds * sizeof(int));
	hdr->commands = (byte*)cmds - (byte*)hdr;

	p = cmds;
	for (i = 0; i < numtris; i++)
	{
		*p++ = 3; // 삼각형 1개 (GL_TRIANGLE_STRIP, 정점 3개)
		for (j = 0; j < 3; j++)
		{
			int vidx = triangles[i].vertindex[j];
			//float s = (stverts[vidx].s + 0.5f) / (float)hdr->skinwidth;
			float s = (float)stverts[vidx].s;
			if (!triangles[i].facesfront && stverts[vidx].onseam)
				s += pheader->skinwidth / 2;
			s = (s + 0.5f) / (float)hdr->skinwidth;

			float t = (stverts[vidx].t + 0.5f) / (float)hdr->skinheight;
			*(float*)p++ = s;
			*(float*)p++ = t;
		}
	}
	*p++ = 0;

	// 2. 모든 프레임(numposes)의 정점 버퍼 할당
	hdr->poseverts = numtris * 3;
	verts = (trivertx_t*)Hunk_Alloc(numposes * hdr->poseverts * sizeof(trivertx_t));
	hdr->posedata = (byte*)verts - (byte*)hdr;

	vout = verts;
	for (f = 0; f < numposes; f++)
	{
		for (i = 0; i < numtris; i++)
		{
			for (j = 0; j < 3; j++)
			{
				int vidx = triangles[i].vertindex[j];
				*vout++ = poseverts[f][vidx];
			}
		}
	}
}

// gl_model.c -> Mod_LoadGS_MDL7 위쪽에 추가
static void GL_MakeGS_DisplayLists0(aliashdr_t* hdr)
{
	int i, j;
	int numtris = hdr->numtris;
	int numcmds = numtris * 7 + 1; // 삼각형당 (count 1개 + 정점 3개 * UV 2개) + 종료 0
	int* cmds;
	int* p;
	trivertx_t* verts;
	trivertx_t* vout;

	// 1. OpenGL 드로우 명령 버퍼 할당
	cmds = (int*)Hunk_Alloc(numcmds * sizeof(int));
	hdr->commands = (byte*)cmds - (byte*)hdr;

	// 2. 정점 버퍼 할당 (삼각형 수 * 3)
	verts = (trivertx_t*)Hunk_Alloc(numtris * 3 * sizeof(trivertx_t));
	hdr->posedata = (byte*)verts - (byte*)hdr;
	hdr->poseverts = numtris * 3;

	p = cmds;
	vout = verts;

	for (i = 0; i < numtris; i++)
	{
		*p++ = 3; // 양수 3 = GL_TRIANGLE_STRIP으로 정점 3개(= 삼각형 1개) 그리기

		for (j = 0; j < 3; j++)
		{
			int vidx = triangles[i].vertindex[j];
			float s = (stverts[vidx].s + 0.5f) / (float)hdr->skinwidth;
			float t = (stverts[vidx].t + 0.5f) / (float)hdr->skinheight;

			*(float*)p++ = s;
			*(float*)p++ = t;
			*vout++ = poseverts[0][vidx];
		}
	}
	*p++ = 0; // 드로우 명령 종료 표시
}

/*
void Mod_LoadGS_MDL7(model_t* mod, void* buffer)
{
	int i, j, start, end, total;
	mdl7_header_t* hdr = (mdl7_header_t*)buffer;
	mdl7_group_t* grp;
	byte* ptr;
	int numverts, numtris, numskins, num_stpts;
	float minv[3] = { 1e9f, 1e9f, 1e9f }, maxv[3] = { -1e9f, -1e9f, -1e9f };
	mdl7_vec3_t* fverts;
	trivertx_t* qverts;

	start = Hunk_LowMark();
	ptr = (byte*)(hdr + 1); // 48바이트 헤더 이후

	// 1. Bones 영역 건너뛰기
	if (hdr->bones_num > 0)
		ptr += hdr->bones_num * (hdr->bone_trf_size + 24);

	// 2. 첫 번째 Group 헤더 읽기 (44바이트)
	grp = (mdl7_group_t*)ptr;
	ptr += sizeof(mdl7_group_t);

	numskins = grp->numskins;
	num_stpts = grp->num_stpts;
	numtris = grp->numtris;
	numverts = grp->numverts;

	if (numverts > MAXALIASVERTS || numtris > MAXALIASTRIS)
		Sys_Error("Mod_LoadGS_MDL7: %s exceeds limits (v:%d, t:%d)", mod->name, numverts, numtris);

	pheader = Hunk_AllocName(sizeof(aliashdr_t), loadname);
	mod->type = mod_alias;
	pheader->numskins = (numskins > 0) ? numskins : 1;
	pheader->numverts = numverts;
	pheader->poseverts = numverts; // 이 줄 추가
	pheader->numtris = numtris;
	pheader->numframes = 1;
	pheader->numposes = 1;
	pheader->skinwidth = 256;
	pheader->skinheight = 256;

	// 3. Skins 읽기 (검증된 28바이트 헤더 + 포맷별 BPP)
	for (i = 0; i < numskins; i++)
	{
		mdl7_skin_t* sk = (mdl7_skin_t*)ptr;
		int format = sk->typ & 0x0F;
		int bpp = 0;
		int imgsize = 0;
		int has_mipmap = (format >= 10 && format <= 13);

		ptr += sizeof(mdl7_skin_t); // 고정 28바이트

		if (format == 2 || format == 3 || format == 10 || format == 11) bpp = 2;
		else if (format == 4 || format == 12)                           bpp = 3;
		else if (format == 5 || format == 13)                           bpp = 4;
		else if (format == 6)                                           imgsize = sk->width; // DDS

		if (bpp > 0)
		{
			imgsize = sk->width * sk->height * bpp;
			if (i == 0)
			{
				pheader->skinwidth = sk->width;
				pheader->skinheight = sk->height;
			}
			if (i < MAX_SKINS)
			{
				pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = GL_UploadGS_Skin(ptr, sk->width, sk->height, sk->typ);
			}
			if (has_mipmap)
				imgsize += (imgsize / 4) + (imgsize / 16) + (imgsize / 64);
		}

		ptr += imgsize;

		if (sk->typ & 0x10)
			ptr += hdr->material_size;
	}

	// 4. Skinpoints (UV) 읽기
	for (i = 0; i < num_stpts; i++)
	{
		mdl7_skinpt_t* pt = (mdl7_skinpt_t*)ptr;
		if (i < MAXALIASVERTS)
		{
			stverts[i].onseam = 0;
			stverts[i].s = (int)(pt->u * pheader->skinwidth);
			stverts[i].t = (int)(pt->v * pheader->skinheight);
		}
		ptr += hdr->skinpoint_size;
	}

	// 5. Triangles 읽기
	for (i = 0; i < numtris; i++)
	{
		short* vidx = (short*)ptr;
		triangles[i].facesfront = 1;
		triangles[i].vertindex[0] = vidx[0];
		triangles[i].vertindex[1] = vidx[1];
		triangles[i].vertindex[2] = vidx[2];
		ptr += hdr->triangle_size;
	}

	// 6. Main Vertices (float x, y, z) 읽기 및 바운딩 박스 계산
	fverts = (mdl7_vec3_t*)malloc(numverts * sizeof(mdl7_vec3_t));
	for (i = 0; i < numverts; i++)
	{
		mdl7_vec3_t* v = (mdl7_vec3_t*)ptr;
		fverts[i] = *v;
		if (v->x < minv[0]) minv[0] = v->x; if (v->x > maxv[0]) maxv[0] = v->x;
		if (v->y < minv[1]) minv[1] = v->y; if (v->y > maxv[1]) maxv[1] = v->y;
		if (v->z < minv[2]) minv[2] = v->z; if (v->z > maxv[2]) maxv[2] = v->z;
		ptr += hdr->mainvertex_size;
	}

	// 7. Quake 1의 0~255 바이트 좌표로 변환
	for (j = 0; j < 3; j++)
	{
		pheader->scale_origin[j] = minv[j];
		pheader->scale[j] = (maxv[j] - minv[j]) / 255.0f;
		if (pheader->scale[j] == 0.0f) pheader->scale[j] = 1.0f;
	}

	qverts = Hunk_AllocName(numverts * sizeof(trivertx_t), loadname);
	for (i = 0; i < numverts; i++)
	{
		qverts[i].v[0] = (byte)((fverts[i].x - pheader->scale_origin[0]) / pheader->scale[0]);
		qverts[i].v[1] = (byte)((fverts[i].y - pheader->scale_origin[1]) / pheader->scale[1]);
		qverts[i].v[2] = (byte)((fverts[i].z - pheader->scale_origin[2]) / pheader->scale[2]);
		qverts[i].lightnormalindex = 0;
	}
	free(fverts);

	pheader->frames[0].firstpose = 0;
	pheader->frames[0].numposes = 1;
	pheader->frames[0].interval = 0.1f;
	strcpy(pheader->frames[0].name, "frame0");
	poseverts[0] = qverts;

	// 8. GL 디스플레이 리스트 생성 및 캐시 저장
	//GL_MakeAliasModelDisplayLists(mod, pheader);
	// 새 함수로 교체
	GL_MakeGS_DisplayLists(pheader);

	for (j = 0; j < 3; j++) { mod->mins[j] = minv[j]; mod->maxs[j] = maxv[j]; }

	Con_Printf("MDL7 Loaded: %s (v:%d, t:%d, size:%.1f)\n",
		mod->name, numverts, numtris, maxv[0] - minv[0]);

	end = Hunk_LowMark();
	total = end - start;
	Cache_Alloc(&mod->cache, total, loadname);
	if (!mod->cache.data) return;
	memcpy(mod->cache.data, pheader, total);
	Hunk_FreeToLowMark(start);
}*/
//
byte* stpts_ptr;
byte* tris_ptr;
int* cmds, * p;
trivertx_t* verts, * vout;
//
void Mod_LoadGS_MDL7(model_t* mod, void* buffer)
{
	int i, j, f, k, start, end, total, header_size;
	mdl7_header_t* hdr = (mdl7_header_t*)buffer;
	mdl7_group_t* grp;
	byte* ptr;
	int numverts, numtris, numskins, num_stpts, numframes;
	float minv[3] = { 1e9f, 1e9f, 1e9f }, maxv[3] = { -1e9f, -1e9f, -1e9f };
	float* uv_u = NULL, * uv_v = NULL;
	unsigned short* tri_v = NULL, * tri_uv = NULL;
	mdl7_vec3_t* base_verts;
	mdl7_vec3_t** frame_verts;
	char frame_names[MAXALIASFRAMES][16];
	trivertx_t* qverts, * verts, * vout;
	int* cmds, * p;

	start = Hunk_LowMark();
	ptr = (byte*)(hdr + 1); // 48바이트 헤더 이후

	// 1. Bones 영역 건너뛰기
	if (hdr->bones_num > 0)
		ptr += hdr->bones_num * (hdr->bone_trf_size + 24);

	// 2. 첫 번째 Group 헤더 읽기 (44바이트)
	grp = (mdl7_group_t*)ptr;
	ptr += sizeof(mdl7_group_t);

	numskins = grp->numskins;
	num_stpts = grp->num_stpts;
	numtris = grp->numtris;
	numverts = grp->numverts;
	numframes = grp->numframes;

	if (numframes < 1) numframes = 1;
	if (numframes > MAXALIASFRAMES) numframes = MAXALIASFRAMES;
	if (numverts > MAXALIASVERTS || numtris > MAXALIASTRIS)
		Sys_Error("Mod_LoadGS_MDL7: %s exceeds limits (v:%d, t:%d)", mod->name, numverts, numtris);

	header_size = sizeof(aliashdr_t) + (numframes - 1) * sizeof(maliasframedesc_t);
	pheader = Hunk_AllocName(header_size, loadname);
	mod->type = mod_alias;
	mod->numframes = numframes;

	pheader->numskins = (numskins > 0) ? numskins : 1;
	pheader->numverts = numverts;
	pheader->numtris = numtris;
	pheader->numframes = numframes;
	pheader->numposes = numframes;
	pheader->skinwidth = 256;
	pheader->skinheight = 256;

	// 3. Skins 읽기
	for (i = 0; i < numskins; i++)
	{
		mdl7_skin_t* sk = (mdl7_skin_t*)ptr;
		int format = sk->typ & 0x0F;
		int bpp = 0, imgsize = 0;
		int has_mipmap = (format >= 10 && format <= 13);

		ptr += sizeof(mdl7_skin_t);

		if (format == 2 || format == 3 || format == 10 || format == 11) bpp = 2;
		else if (format == 4 || format == 12)                           bpp = 3;
		else if (format == 5 || format == 13)                           bpp = 4;
		else if (format == 6)                                           imgsize = sk->width;

		if (bpp > 0)
		{
			imgsize = sk->width * sk->height * bpp;
			if (i == 0)
			{
				pheader->skinwidth = sk->width;
				pheader->skinheight = sk->height;
			}
			if (i < MAX_SKINS)
			{
				pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = GL_UploadGS_Skin(ptr, sk->width, sk->height, sk->typ);
			}
			if (has_mipmap)
				imgsize += (imgsize / 4) + (imgsize / 16) + (imgsize / 64);
		}

		ptr += imgsize;
		if (sk->typ & 0x10)
			ptr += hdr->material_size;
	}

	// 4. Skinpoints (UV) 읽기
	if (num_stpts > 0)
	{
		uv_u = (float*)malloc(num_stpts * sizeof(float));
		uv_v = (float*)malloc(num_stpts * sizeof(float));
		for (i = 0; i < num_stpts; i++)
		{
			mdl7_skinpt_t* pt = (mdl7_skinpt_t*)ptr;
			uv_u[i] = pt->u;
			uv_v[i] = pt->v;
			ptr += hdr->skinpoint_size;
		}
	}

	// 5. Triangles 읽기 (정점 인덱스 t[0..2] + UV 인덱스 t[3..5] 분리 저장)
	tri_v = (unsigned short*)malloc(numtris * 3 * sizeof(unsigned short));
	tri_uv = (unsigned short*)malloc(numtris * 3 * sizeof(unsigned short));
	for (i = 0; i < numtris; i++)
	{
		unsigned short* t = (unsigned short*)ptr;
		for (j = 0; j < 3; j++)
		{
			tri_v[i * 3 + j] = t[j];
			tri_uv[i * 3 + j] = (hdr->triangle_size >= 12) ? t[3 + j] : t[j];
		}
		ptr += hdr->triangle_size;
	}

	// 6. Main Vertices 읽기
	base_verts = (mdl7_vec3_t*)malloc(numverts * sizeof(mdl7_vec3_t));
	for (i = 0; i < numverts; i++)
	{
		mdl7_vec3_t* v = (mdl7_vec3_t*)ptr;
		base_verts[i] = *v;
		if (v->x < minv[0]) minv[0] = v->x; if (v->x > maxv[0]) maxv[0] = v->x;
		if (v->y < minv[1]) minv[1] = v->y; if (v->y > maxv[1]) maxv[1] = v->y;
		if (v->z < minv[2]) minv[2] = v->z; if (v->z > maxv[2]) maxv[2] = v->z;
		ptr += hdr->mainvertex_size;
	}

	// 7. 모든 프레임 읽기
	frame_verts = (mdl7_vec3_t**)malloc(numframes * sizeof(mdl7_vec3_t*));
	for (f = 0; f < numframes; f++)
	{
		frame_verts[f] = (mdl7_vec3_t*)malloc(numverts * sizeof(mdl7_vec3_t));
		memcpy(frame_verts[f], base_verts, numverts * sizeof(mdl7_vec3_t));

		if (grp->numframes > 0)
		{
			mdl7_frame_t* fr = (mdl7_frame_t*)ptr;
			memcpy(frame_names[f], fr->name, 16);
			frame_names[f][15] = '\0';
			ptr += hdr->frame_size;

			for (k = 0; k < fr->vertices_count; k++)
			{
				mdl7_framevert_t* fv = (mdl7_framevert_t*)ptr;
				if (fv->vertindex < numverts)
				{
					frame_verts[f][fv->vertindex].x = fv->x;
					frame_verts[f][fv->vertindex].y = fv->y;
					frame_verts[f][fv->vertindex].z = fv->z;
					if (fv->x < minv[0]) minv[0] = fv->x; if (fv->x > maxv[0]) maxv[0] = fv->x;
					if (fv->y < minv[1]) minv[1] = fv->y; if (fv->y > maxv[1]) maxv[1] = fv->y;
					if (fv->z < minv[2]) minv[2] = fv->z; if (fv->z > maxv[2]) maxv[2] = fv->z;
				}
				ptr += hdr->framevertex_size;
			}
			if (fr->transmatrix_count > 0)
				ptr += fr->transmatrix_count * hdr->bonetrans_size;
		}
		else
		{
			sprintf(frame_names[f], "frame%d", f);
		}
	}
	free(base_verts);

	// 8. 0~255 바이트 좌표 변환
	for (j = 0; j < 3; j++)
	{
		pheader->scale_origin[j] = minv[j];
		pheader->scale[j] = (maxv[j] - minv[j]) / 255.0f;
		if (pheader->scale[j] == 0.0f) pheader->scale[j] = 1.0f;
	}

	for (f = 0; f < numframes; f++)
	{
		qverts = Hunk_AllocName(numverts * sizeof(trivertx_t), loadname);
		for (i = 0; i < numverts; i++)
		{
			qverts[i].v[0] = (byte)((frame_verts[f][i].x - pheader->scale_origin[0]) / pheader->scale[0]);
			qverts[i].v[1] = (byte)((frame_verts[f][i].y - pheader->scale_origin[1]) / pheader->scale[1]);
			qverts[i].v[2] = (byte)((frame_verts[f][i].z - pheader->scale_origin[2]) / pheader->scale[2]);
			qverts[i].lightnormalindex = 0;
		}
		free(frame_verts[f]);

		pheader->frames[f].firstpose = f;
		pheader->frames[f].numposes = 1;
		pheader->frames[f].interval = 0.1f;
		strcpy(pheader->frames[f].name, frame_names[f]);
		poseverts[f] = qverts;
	}
	free(frame_verts);

	// 9. OpenGL 디스플레이 리스트 생성 (정점/UV 분리 적용)
	cmds = (int*)Hunk_Alloc((numtris * 7 + 1) * sizeof(int));
	pheader->commands = (byte*)cmds - (byte*)pheader;

	p = cmds;
	for (i = 0; i < numtris; i++)
	{
		*p++ = 3;
		for (j = 0; j < 3; j++)
		{
			unsigned short uvidx = tri_uv[i * 3 + j];
			float u = (uv_u && uvidx < num_stpts) ? uv_u[uvidx] : 0.0f;
			float v = (uv_v && uvidx < num_stpts) ? uv_v[uvidx] : 0.0f;
			*(float*)p++ = u;
			*(float*)p++ = v;
		}
	}
	*p++ = 0;

	pheader->poseverts = numtris * 3;
	verts = (trivertx_t*)Hunk_Alloc(numframes * pheader->poseverts * sizeof(trivertx_t));
	pheader->posedata = (byte*)verts - (byte*)pheader;

	vout = verts;
	for (f = 0; f < numframes; f++)
	{
		for (i = 0; i < numtris; i++)
		{
			for (j = 0; j < 3; j++)
			{
				unsigned short vidx = tri_v[i * 3 + j];
				if (vidx >= numverts) vidx = 0;
				*vout++ = poseverts[f][vidx];
			}
		}
	}

	if (uv_u) free(uv_u);
	if (uv_v) free(uv_v);
	free(tri_v);
	free(tri_uv);

	for (j = 0; j < 3; j++) { mod->mins[j] = minv[j]; mod->maxs[j] = maxv[j]; }

	Con_Printf("MDL7 Loaded: %s (v:%d, t:%d, f:%d)\n", mod->name, numverts, numtris, numframes);

	end = Hunk_LowMark();
	total = end - start;
	Cache_Alloc(&mod->cache, total, loadname);
	if (!mod->cache.data) return;
	memcpy(mod->cache.data, pheader, total);
	Hunk_FreeToLowMark(start);
}
//
void Mod_LoadGS_MDL7_(model_t* mod, void* buffer)
{
	int i, j, start, end, total;
	mdl7_header_t* hdr = (mdl7_header_t*)buffer;
	mdl7_group_t* grp;
	byte* ptr;
	int numverts, numtris, numskins, num_stpts;
	float minv[3] = { 1e9f, 1e9f, 1e9f }, maxv[3] = { -1e9f, -1e9f, -1e9f };
	mdl7_vec3_t* fverts;
	trivertx_t* qverts;

	start = Hunk_LowMark();
	ptr = (byte*)(hdr + 1); // 48바이트 헤더 이후

	// 1. Bones 영역 건너뛰기
	if (hdr->bones_num > 0)
		ptr += hdr->bones_num * (hdr->bone_trf_size + 24);

	// 2. 첫 번째 Group 헤더 읽기 (44바이트)
	grp = (mdl7_group_t*)ptr;
	ptr += sizeof(mdl7_group_t);

	numskins = grp->numskins;
	num_stpts = grp->num_stpts;
	numtris = grp->numtris;
	numverts = grp->numverts;

	if (numverts > MAXALIASVERTS || numtris > MAXALIASTRIS)
		Sys_Error("Mod_LoadGS_MDL7: %s exceeds limits (v:%d, t:%d)", mod->name, numverts, numtris);

	pheader = Hunk_AllocName(sizeof(aliashdr_t), loadname);
	mod->type = mod_alias;
	pheader->numskins = (numskins > 0) ? numskins : 1;
	pheader->numverts = numverts;
	pheader->poseverts = numverts; // 이 줄 추가
	pheader->numtris = numtris;
	pheader->numframes = 1;
	pheader->numposes = 1;
	pheader->skinwidth = 256;
	pheader->skinheight = 256;

	

	//
	// 
	int numframes = grp->numframes;
	int f, k, header_size;
	mdl7_vec3_t* base_verts;
	mdl7_vec3_t** frame_verts;
	char frame_names[MAXALIASFRAMES][16];

	if (numframes < 1) numframes = 1;
	if (numframes > MAXALIASFRAMES) numframes = MAXALIASFRAMES;

	// frames[numframes] 배열 공간을 포함해 pheader 할당
	header_size = sizeof(aliashdr_t) + (numframes - 1) * sizeof(maliasframedesc_t);
	pheader = Hunk_AllocName(header_size, loadname);
	mod->type = mod_alias;
	mod->numframes = numframes;
	pheader->numskins = (numskins > 0) ? numskins : 1;
	pheader->numverts = numverts;
	pheader->numtris = numtris;
	pheader->numframes = numframes;
	pheader->numposes = numframes;
	pheader->skinwidth = 256;
	pheader->skinheight = 256;
	//

	// 3. Skins 읽기 (검증된 28바이트 헤더 + 포맷별 BPP)
	for (i = 0; i < numskins; i++)
	{
		mdl7_skin_t* sk = (mdl7_skin_t*)ptr;
		int format = sk->typ & 0x0F;
		int bpp = 0;
		int imgsize = 0;
		int has_mipmap = (format >= 10 && format <= 13);

		ptr += sizeof(mdl7_skin_t); // 고정 28바이트

		if (format == 2 || format == 3 || format == 10 || format == 11) bpp = 2;
		else if (format == 4 || format == 12)                           bpp = 3;
		else if (format == 5 || format == 13)                           bpp = 4;
		else if (format == 6)                                           imgsize = sk->width; // DDS

		if (bpp > 0)
		{
			imgsize = sk->width * sk->height * bpp;
			if (i == 0)
			{
				pheader->skinwidth = sk->width;
				pheader->skinheight = sk->height;
			}
			if (i < MAX_SKINS)
			{
				pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = GL_UploadGS_Skin(ptr, sk->width, sk->height, sk->typ);
			}
			if (has_mipmap)
				imgsize += (imgsize / 4) + (imgsize / 16) + (imgsize / 64);
		}

		ptr += imgsize;

		if (sk->typ & 0x10)
			ptr += hdr->material_size;
	}

	// 4. Skinpoints (UV) 읽기
	for (i = 0; i < num_stpts; i++)
	{
		mdl7_skinpt_t* pt = (mdl7_skinpt_t*)ptr;
		if (i < MAXALIASVERTS)
		{
			stverts[i].onseam = 0;
			stverts[i].s = (int)(pt->u * pheader->skinwidth);
			stverts[i].t = (int)(pt->v * pheader->skinheight);
		}
		ptr += hdr->skinpoint_size;
	}
	// 4. Skinpoints (UV) 위치 저장
	stpts_ptr = ptr;
	ptr += num_stpts * hdr->skinpoint_size;


	// 5. Triangles 읽기
	for (i = 0; i < numtris; i++)
	{
		short* vidx = (short*)ptr;
		triangles[i].facesfront = 1;
		triangles[i].vertindex[0] = vidx[0];
		triangles[i].vertindex[1] = vidx[1];
		triangles[i].vertindex[2] = vidx[2];
		ptr += hdr->triangle_size;
	}

	// 5. Triangles 위치 저장
	tris_ptr = ptr;
	ptr += numtris * hdr->triangle_size;

	//
	// 6. Main Vertices (기본 포즈) 읽기
	base_verts = (mdl7_vec3_t*)malloc(numverts * sizeof(mdl7_vec3_t));
	for (i = 0; i < numverts; i++)
	{
		mdl7_vec3_t* v = (mdl7_vec3_t*)ptr;
		base_verts[i] = *v;
		if (v->x < minv[0]) minv[0] = v->x; if (v->x > maxv[0]) maxv[0] = v->x;
		if (v->y < minv[1]) minv[1] = v->y; if (v->y > maxv[1]) maxv[1] = v->y;
		if (v->z < minv[2]) minv[2] = v->z; if (v->z > maxv[2]) maxv[2] = v->z;
		ptr += hdr->mainvertex_size;
	}

	// 6-2. 모든 프레임(Frames) 읽기
	frame_verts = (mdl7_vec3_t**)malloc(numframes * sizeof(mdl7_vec3_t*));
	for (f = 0; f < numframes; f++)
	{
		mdl7_frame_t* fr = (mdl7_frame_t*)ptr;
		frame_verts[f] = (mdl7_vec3_t*)malloc(numverts * sizeof(mdl7_vec3_t));

		// 기본 정점 복사 후, 이 프레임에서 변경된 정점만 덮어쓰기
		memcpy(frame_verts[f], base_verts, numverts * sizeof(mdl7_vec3_t));

		if (grp->numframes > 0)
		{
			memcpy(frame_names[f], fr->name, 16);
			frame_names[f][15] = '\0';
			ptr += hdr->frame_size;

			for (k = 0; k < fr->vertices_count; k++)
			{
				mdl7_framevert_t* fv = (mdl7_framevert_t*)ptr;
				if (fv->vertindex < numverts)
				{
					frame_verts[f][fv->vertindex].x = fv->x;
					frame_verts[f][fv->vertindex].y = fv->y;
					frame_verts[f][fv->vertindex].z = fv->z;

					if (fv->x < minv[0]) minv[0] = fv->x; if (fv->x > maxv[0]) maxv[0] = fv->x;
					if (fv->y < minv[1]) minv[1] = fv->y; if (fv->y > maxv[1]) maxv[1] = fv->y;
					if (fv->z < minv[2]) minv[2] = fv->z; if (fv->z > maxv[2]) maxv[2] = fv->z;
				}
				ptr += hdr->framevertex_size;
			}

			// 본 변환 행렬 영역 건너뛰기
			if (fr->transmatrix_count > 0)
				ptr += fr->transmatrix_count * hdr->bonetrans_size;
		}
		else
		{
			sprintf(frame_names[f], "frame%d", f);
		}
	}
	free(base_verts);

	// 7. 전체 프레임 통합 바운딩 박스로 0~255 압축
	for (j = 0; j < 3; j++)
	{
		pheader->scale_origin[j] = minv[j];
		pheader->scale[j] = (maxv[j] - minv[j]) / 255.0f;
		if (pheader->scale[j] == 0.0f) pheader->scale[j] = 1.0f;
	}

	for (f = 0; f < numframes; f++)
	{
		qverts = Hunk_AllocName(numverts * sizeof(trivertx_t), loadname);
		for (i = 0; i < numverts; i++)
		{
			qverts[i].v[0] = (byte)((frame_verts[f][i].x - pheader->scale_origin[0]) / pheader->scale[0]);
			qverts[i].v[1] = (byte)((frame_verts[f][i].y - pheader->scale_origin[1]) / pheader->scale[1]);
			qverts[i].v[2] = (byte)((frame_verts[f][i].z - pheader->scale_origin[2]) / pheader->scale[2]);
			qverts[i].lightnormalindex = 0;
		}
		free(frame_verts[f]);

		pheader->frames[f].firstpose = f;
		pheader->frames[f].numposes = 1;
		pheader->frames[f].interval = 0.1f;
		strcpy(pheader->frames[f].name, frame_names[f]);
		poseverts[f] = qverts;
	}
	free(frame_verts);

	// 8. GL 디스플레이 리스트 생성 및 캐시 저장
	//GL_MakeGS_DisplayLists(pheader);
	
	// 8. GL 디스플레이 리스트 생성 (MDL7 전용: 메모리 범위 검사 포함)
	cmds = (int*)Hunk_Alloc((numtris * 7 + 1) * sizeof(int));
	pheader->commands = (byte*)cmds - (byte*)pheader;

	if (numtris > 0)
	{
		unsigned short* t0 = (unsigned short*)tris_ptr;
		Con_Printf("MDL7 Debug: tri_size=%d, stpts=%d, t0=[%u,%u,%u | %u,%u,%u]\n",
			hdr->triangle_size, num_stpts, t0[0], t0[1], t0[2], t0[3], t0[4], t0[5]);
	}

	p = cmds;
	for (i = 0; i < numtris; i++)
	{
		unsigned short* t = (unsigned short*)(tris_ptr + i * hdr->triangle_size);
		*p++ = 3; // 삼각형 1개

		for (j = 0; j < 3; j++)
		{
			unsigned short uvidx = (hdr->triangle_size >= 12) ? t[3 + j] : t[j];
			float u = 0.0f, v = 0.0f;

			if (uvidx < (unsigned short)num_stpts)
			{
				mdl7_skinpt_t* pt = (mdl7_skinpt_t*)(stpts_ptr + uvidx * hdr->skinpoint_size);
				u = pt->u;
				v = pt->v;
			}
			*(float*)p++ = u;
			*(float*)p++ = v;
		}
	}
	*p++ = 0;

	pheader->poseverts = numtris * 3;
	verts = (trivertx_t*)Hunk_Alloc(numframes * pheader->poseverts * sizeof(trivertx_t));
	pheader->posedata = (byte*)verts - (byte*)pheader;

	vout = verts;
	for (f = 0; f < numframes; f++)
	{
		for (i = 0; i < numtris; i++)
		{
			unsigned short* t = (unsigned short*)(tris_ptr + i * hdr->triangle_size);
			for (j = 0; j < 3; j++)
			{
				unsigned short vidx = t[j];
				if (vidx >= (unsigned short)numverts)
					vidx = 0;
				*vout++ = poseverts[f][vidx];
			}
		}
	}
	//
	// 
	
	for (j = 0; j < 3; j++) { mod->mins[j] = minv[j]; mod->maxs[j] = maxv[j]; }

	Con_Printf("MDL7 Loaded: %s (v:%d, t:%d, size:%.1f)\n",
		mod->name, numverts, numtris, maxv[0] - minv[0]);

	end = Hunk_LowMark();
	total = end - start;
	Cache_Alloc(&mod->cache, total, loadname);
	if (!mod->cache.data) return;
	memcpy(mod->cache.data, pheader, total);
	Hunk_FreeToLowMark(start);
}

//
// gl_model.c 에 추가
void Mod_LoadGS_MDL(model_t* mod, void* buffer)
{
	int i, j, start, end, total;
	gs_mdl_header_t* pinmodel;
	gs_mdl5_ext_t* pinext = NULL;
	byte* ptr;
	int ident, numstverts;
	int skinwidth, skinheight, numskins, numverts, numtris, numframes;

	start = Hunk_LowMark();
	pinmodel = (gs_mdl_header_t*)buffer;
	ident = LittleLong(pinmodel->ident);

	numskins = LittleLong(pinmodel->numskins);
	skinwidth = LittleLong(pinmodel->skinwidth);
	skinheight = LittleLong(pinmodel->skinheight);
	numverts = LittleLong(pinmodel->numverts);
	numtris = LittleLong(pinmodel->numtris);
	numframes = LittleLong(pinmodel->numframes);

	if (numverts > MAXALIASVERTS)
		Sys_Error("Mod_LoadGS_MDL: %s has too many vertices (%d)", mod->name, numverts);
	if (numtris > MAXALIASTRIS)
		Sys_Error("Mod_LoadGS_MDL: %s has too many triangles (%d)", mod->name, numtris);

	// 1. 퀘이크 aliashdr_t 메모리 할당
	pheader = Hunk_AllocName(sizeof(aliashdr_t) + (numframes - 1) * sizeof(pheader->frames[0]), loadname);

	mod->type = mod_alias;
	mod->flags = LittleLong(pinmodel->flags);

	pheader->numskins = numskins;
	pheader->skinwidth = skinwidth;
	pheader->skinheight = skinheight;
	pheader->numverts = numverts;
	pheader->numtris = numtris;
	pheader->numframes = numframes;
	pheader->numposes = numframes;
	pheader->size = LittleFloat(pinmodel->size) * ALIAS_BASE_SIZE_RATIO;

	for (i = 0; i < 3; i++)
	{
		pheader->scale[i] = LittleFloat(pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat(pinmodel->scale_origin[i]);
	}

	// 데이터 포인터 이동 (헤더 직후)
	ptr = (byte*)(pinmodel + 1);

	if (ident == IDMDL5HEADER)
	{
		pinext = (gs_mdl5_ext_t*)ptr;
		numstverts = LittleLong(pinext->num_stverts);
		ptr += sizeof(gs_mdl5_ext_t);
	}
	else
	{
		numstverts = numverts;
	}

	// 2. 스킨(텍스처) 로딩
	for (i = 0; i < numskins; i++)
	{
		int skintype = LittleLong(*(int*)ptr);
		int format = skintype & 0x0F;
		int bpp = (format == 2) ? 2 : (format == 3) ? 3 : (format == 4) ? 4 : 1;
		int skinbytes = skinwidth * skinheight * bpp;

		ptr += sizeof(int); // skintype 건너뛰기

		if (i < MAX_SKINS)
		{
			pheader->gl_texturenum[i][0] =
				pheader->gl_texturenum[i][1] =
				pheader->gl_texturenum[i][2] =
				pheader->gl_texturenum[i][3] = GL_UploadGS_Skin(ptr, skinwidth, skinheight, skintype);
		}

		ptr += skinbytes;
		// 주의: 스킨에 밉맵 플래그(skintype & 0x10 등)가 켜져 있다면 밉맵 크기만큼 추가 이동 필요
	}

	// 3. ST(UV) 좌표 로딩
	if (ident == IDMDL5HEADER)
	{
		// MDL5는 (short s, short t) 배열로 저장됨
		short* pinst = (short*)ptr;
		for (i = 0; i < numstverts && i < MAXALIASVERTS; i++)
		{
			stverts[i].onseam = 0;
			stverts[i].s = LittleShort(pinst[i * 2 + 0]);
			stverts[i].t = LittleShort(pinst[i * 2 + 1]);
		}
		ptr += numstverts * sizeof(short) * 2;
	}
	else
	{
		// MDL3/4는 Quake 1의 dstvert_t와 동일
		stvert_t* pinst = (stvert_t*)ptr;
		for (i = 0; i < numverts; i++)
		{
			stverts[i].onseam = LittleLong(pinst[i].onseam);
			stverts[i].s = LittleLong(pinst[i].s);
			stverts[i].t = LittleLong(pinst[i].t);
		}
		ptr += numverts * sizeof(stvert_t);
	}

	// 4. 트라이앵글 인덱스 로딩
	if (ident == IDMDL5HEADER)
	{
		gs_mdl5_triangle_t* pintri = (gs_mdl5_triangle_t*)ptr;
		for (i = 0; i < numtris; i++)
		{
			triangles[i].facesfront = 1;
			for (j = 0; j < 3; j++)
				triangles[i].vertindex[j] = LittleShort(pintri[i].vertindex[j]);
		}
		ptr += numtris * sizeof(gs_mdl5_triangle_t);
	}
	else
	{
		dtriangle_t* pintri = (dtriangle_t*)ptr;
		for (i = 0; i < numtris; i++)
		{
			triangles[i].facesfront = LittleLong(pintri[i].facesfront);
			for (j = 0; j < 3; j++)
				triangles[i].vertindex[j] = LittleLong(pintri[i].vertindex[j]);
		}
		ptr += numtris * sizeof(dtriangle_t);
	}

	// 5. 애니메이션 프레임 로딩
	posenum = 0;
	for (i = 0; i < numframes; i++)
	{
		int frametype = LittleLong(*(int*)ptr);
		ptr += sizeof(int);

		// 단순 프레임 기준 (daliasframe_t: bboxmin, bboxmax, name[16] + trivertx_t[numverts])
		daliasframe_t* pinframe = (daliasframe_t*)ptr;

		pheader->frames[i].firstpose = posenum;
		pheader->frames[i].numposes = 1;
		pheader->frames[i].interval = 0.1f;
		memcpy(pheader->frames[i].name, pinframe->name, 16);

		for (j = 0; j < 3; j++)
		{
			pheader->frames[i].bboxmin.v[j] = pinframe->bboxmin.v[j];
			pheader->frames[i].bboxmax.v[j] = pinframe->bboxmax.v[j];
		}

		ptr += sizeof(daliasframe_t);
		poseverts[posenum] = (trivertx_t*)ptr;
		posenum++;

		ptr += numverts * sizeof(trivertx_t);
	}

	// 6. GL 렌더링용 디스플레이 리스트 생성 (기존 GLQuake 함수 그대로 활용)
	GL_MakeAliasModelDisplayLists(mod, pheader);

	// 7. 모델 바운딩 박스 설정 및 캐시 이동
	mod->mins[0] = mod->mins[1] = mod->mins[2] = -32;
	mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 32;

	end = Hunk_LowMark();
	total = end - start;
	Cache_Alloc(&mod->cache, total, loadname);
	if (!mod->cache.data)
		return;
	memcpy(mod->cache.data, pheader, total);
	Hunk_FreeToLowMark(start);
}

// gl_model.c 에 추가
/*
void Mod_LoadGS_MDL7(model_t* mod, void* buffer)
{
	int i, j, start, end, total;
	mdl7_header_t* hdr = (mdl7_header_t*)buffer;
	mdl7_group_t* grp;
	byte* ptr;
	int numverts, numtris, numskins, num_stpts;
	float minv[3] = { 1e9f, 1e9f, 1e9f }, maxv[3] = { -1e9f, -1e9f, -1e9f };
	mdl7_vec3_t* fverts;
	trivertx_t* qverts;

	start = Hunk_LowMark();
	ptr = (byte*)(hdr + 1);

	// 1. Bones 영역 건너뛰기 (earth.mdl은 0개)
	if (hdr->bones_num > 0)
		ptr += hdr->bones_num * (hdr->bone_trf_size + 4 + 20); // 간략화 (본 없는 모델 기준)

	// 2. 첫 번째 Group 헤더 읽기
	grp = (mdl7_group_t*)ptr;
	ptr += sizeof(mdl7_group_t);

	numskins = grp->numskins;
	num_stpts = grp->num_stpts;
	numtris = grp->numtris;
	numverts = grp->numverts;

	if (numverts > MAXALIASVERTS || numtris > MAXALIASTRIS)
		Sys_Error("Mod_LoadGS_MDL7: %s exceeds limits (v:%d, t:%d)", mod->name, numverts, numtris);

	pheader = Hunk_AllocName(sizeof(aliashdr_t), loadname);
	mod->type = mod_alias;
	pheader->numskins = (numskins > 0) ? numskins : 1;
	pheader->numverts = numverts;
	pheader->numtris = numtris;
	pheader->numframes = 1;
	pheader->numposes = 1;
	pheader->skinwidth = 256;
	pheader->skinheight = 256;

	
	//
	// 3. Skins 읽기
	for (i = 0; i < numskins; i++)
	{
		mdl7_skin_t* sk = (mdl7_skin_t*)ptr;
		int format = sk->typ & 0x0F;
		int bpp = 0;
		int imgsize = 0;
		int has_mipmap = 0;

		Con_Printf("MDL7 Skin[%d]: typ=0x%02X, %dx%d (skin_size=%d)\n",
			i, sk->typ, sk->width, sk->height, hdr->skin_size);

		// 헤더 크기만큼 이동 (구조체 크기 대신 파일 헤더에 기록된 skin_size 사용)
		ptr += hdr->skin_size;

		// 포맷별 바이트 수 계산
		if (format == 2 || format == 10) { bpp = 2; has_mipmap = (format == 10); } // 16-bit
		else if (format == 3 || format == 11) { bpp = 3; has_mipmap = (format == 11); } // 24-bit
		else if (format == 4 || format == 12) { bpp = 4; has_mipmap = (format == 12); } // 32-bit
		else if (format == 6) { imgsize = sk->width; }                  // DDS (width에 전체 바이트 크기 저장)

		if (bpp > 0)
		{
			imgsize = sk->width * sk->height * bpp;
			if (i == 0)
			{
				pheader->skinwidth = sk->width;
				pheader->skinheight = sk->height;
			}
			if (i < MAX_SKINS)
			{
				// 10/11/12는 2/3/4 포맷으로 변환해서 업로드
				int upload_fmt = (format >= 10) ? (format - 8) : format;
				pheader->gl_texturenum[i][0] =
					pheader->gl_texturenum[i][1] =
					pheader->gl_texturenum[i][2] =
					pheader->gl_texturenum[i][3] = GL_UploadGS_Skin(ptr, sk->width, sk->height, upload_fmt);
			}
			if (has_mipmap)
				imgsize += (imgsize / 4) + (imgsize / 16) + (imgsize / 64);
		}

		ptr += imgsize;

		// 머티리얼 데이터(0x10)가 포함된 경우 건너뛰기
		if (sk->typ & 0x10)
			ptr += hdr->material_size;
	}

	//

	// 4. Skinpoints (UV) 읽기
	{
		mdl7_skinpt_t* pts = (mdl7_skinpt_t*)ptr;
		for (i = 0; i < num_stpts && i < MAXALIASVERTS; i++)
		{
			stverts[i].onseam = 0;
			stverts[i].s = (int)(pts[i].u * pheader->skinwidth);
			stverts[i].t = (int)(pts[i].v * pheader->skinheight);
		}
		ptr += num_stpts * hdr->skinpoint_size;
	}

	// 5. Triangles 읽기
	for (i = 0; i < numtris; i++)
	{
		short* vidx = (short*)ptr;
		triangles[i].facesfront = 1;
		triangles[i].vertindex[0] = vidx[0];
		triangles[i].vertindex[1] = vidx[1];
		triangles[i].vertindex[2] = vidx[2];
		ptr += hdr->triangle_size;
	}

	// 6. Main Vertices (float x, y, z) 읽기 및 바운딩 박스 계산
	fverts = (mdl7_vec3_t*)malloc(numverts * sizeof(mdl7_vec3_t));
	for (i = 0; i < numverts; i++)
	{
		mdl7_vec3_t* v = (mdl7_vec3_t*)ptr;
		fverts[i] = *v;
		if (v->x < minv[0]) minv[0] = v->x; if (v->x > maxv[0]) maxv[0] = v->x;
		if (v->y < minv[1]) minv[1] = v->y; if (v->y > maxv[1]) maxv[1] = v->y;
		if (v->z < minv[2]) minv[2] = v->z; if (v->z > maxv[2]) maxv[2] = v->z;
		ptr += hdr->mainvertex_size;
	}

	// 7. Quake 1의 0~255 바이트 좌표로 변환
	for (j = 0; j < 3; j++)
	{
		pheader->scale_origin[j] = minv[j];
		pheader->scale[j] = (maxv[j] - minv[j]) / 255.0f;
		if (pheader->scale[j] == 0.0f) pheader->scale[j] = 1.0f;
	}

	qverts = Hunk_AllocName(numverts * sizeof(trivertx_t), loadname);
	for (i = 0; i < numverts; i++)
	{
		qverts[i].v[0] = (byte)((fverts[i].x - pheader->scale_origin[0]) / pheader->scale[0]);
		qverts[i].v[1] = (byte)((fverts[i].y - pheader->scale_origin[1]) / pheader->scale[1]);
		qverts[i].v[2] = (byte)((fverts[i].z - pheader->scale_origin[2]) / pheader->scale[2]);
		qverts[i].lightnormalindex = 0;
	}
	free(fverts);

	pheader->frames[0].firstpose = 0;
	pheader->frames[0].numposes = 1;
	pheader->frames[0].interval = 0.1f;
	strcpy(pheader->frames[0].name, "frame0");
	poseverts[0] = qverts;

	// 8. 디스플레이 리스트 생성 및 캐시 저장
	GL_MakeAliasModelDisplayLists(mod, pheader);

	for (j = 0; j < 3; j++) { mod->mins[j] = minv[j]; mod->maxs[j] = maxv[j]; }

	end = Hunk_LowMark();
	total = end - start;
	Cache_Alloc(&mod->cache, total, loadname);
	if (!mod->cache.data) return;
	memcpy(mod->cache.data, pheader, total);
	Hunk_FreeToLowMark(start);
} */
//