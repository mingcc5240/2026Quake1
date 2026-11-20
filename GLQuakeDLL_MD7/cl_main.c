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
// cl_main.c  -- client main loop

#include "quakedef.h"

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t	cl_name = {"_cl_name", "player", true};
cvar_t	cl_color = {"_cl_color", "0", true};

cvar_t	cl_shownet = {"cl_shownet","0"};	// can be 0, 1, or 2
cvar_t	cl_nolerp = {"cl_nolerp","0"};

cvar_t	lookspring = {"lookspring","0", true};
cvar_t	lookstrafe = {"lookstrafe","0", true};
cvar_t	sensitivity = {"sensitivity","3", true};

cvar_t	m_pitch = {"m_pitch","0.022", true};
cvar_t	m_yaw = {"m_yaw","0.022", true};
cvar_t	m_forward = {"m_forward","1", true};
cvar_t	m_side = {"m_side","0.8", true};


client_static_t	cls;
client_state_t	cl;
// FIXME: put these on hunk?
efrag_t			cl_efrags[MAX_EFRAGS];
entity_t		cl_entities[MAX_EDICTS];
entity_t		cl_static_entities[MAX_STATIC_ENTITIES];
lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
dlight_t		cl_dlights[MAX_DLIGHTS];

int				cl_numvisedicts;
entity_t		*cl_visedicts[MAX_VISEDICTS];


// cl_main.c -> static_anim_t 수정
typedef struct {
	int   start_frame;
	int   end_frame;
	float fps;
	int   ai_mode;   // 0:회전, 1:바라보기, 2:따라오기, 3:정지, 4:목표지점 이동
	float min_z;
	float speed;     // 이동 속도 (기본 65.0)
	float rot_speed; // 회전 속도 (초당 각도, 기본 20.0)
	float target_x;  // 모드 4용 목표 X
	float target_y;  // 모드 4용 목표 Y
} static_anim_t;

static static_anim_t cl_static_anims[MAX_STATIC_ENTITIES];

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	int			i;

	if (!sv.active)
		Host_ClearMemory ();

// wipe the entire cl structure
	memset (&cl, 0, sizeof(cl));

	SZ_Clear (&cls.message);

// clear other arrays	
	memset (cl_efrags, 0, sizeof(cl_efrags));
	memset (cl_entities, 0, sizeof(cl_entities));
	memset (cl_dlights, 0, sizeof(cl_dlights));
	memset (cl_lightstyle, 0, sizeof(cl_lightstyle));
	memset (cl_temp_entities, 0, sizeof(cl_temp_entities));
	memset (cl_beams, 0, sizeof(cl_beams));

//
// allocate the efrags and chain together into a free list
//
	cl.free_efrags = cl_efrags;
	for (i=0 ; i<MAX_EFRAGS-1 ; i++)
		cl.free_efrags[i].entnext = &cl.free_efrags[i+1];
	cl.free_efrags[i].entnext = NULL;
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect (void)
{
// stop sounds (especially looping!)
	S_StopAllSounds (true);
	
// bring the console down and fade the colors back to normal
//	SCR_BringDownConsole ();

// if running a local server, shut it down
	if (cls.demoplayback)
		CL_StopPlayback ();
	else if (cls.state == ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f ();

		Con_DPrintf ("Sending clc_disconnect\n");
		SZ_Clear (&cls.message);
		MSG_WriteByte (&cls.message, clc_disconnect);
		NET_SendUnreliableMessage (cls.netcon, &cls.message);
		SZ_Clear (&cls.message);
		NET_Close (cls.netcon);

		cls.state = ca_disconnected;
		if (sv.active)
			Host_ShutdownServer(false);
	}

	cls.demoplayback = cls.timedemo = false;
	cls.signon = 0;
}

void CL_Disconnect_f (void)
{
	CL_Disconnect ();
	if (sv.active)
		Host_ShutdownServer (false);
}




/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection (char *host)
{
	if (cls.state == ca_dedicated)
		return;

	if (cls.demoplayback)
		return;

	CL_Disconnect ();

	cls.netcon = NET_Connect (host);
	if (!cls.netcon)
		Host_Error ("CL_Connect: connect failed\n");
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);
	
	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;				// need all the signon messages before playing
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply (void)
{
	char 	str[8192];

Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "prespawn");
		break;
		
	case 2:		
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("name \"%s\"\n", cl_name.string));
	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("color %i %i\n", ((int)cl_color.value)>>4, ((int)cl_color.value)&15));
	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		sprintf (str, "spawn %s", cls.spawnparms);
		MSG_WriteString (&cls.message, str);
		break;
		
	case 3:	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		Cache_Report ();		// print remaining memory
		break;
		
	case 4:
		SCR_EndLoadingPlaque ();		// allow normal screen updates
		break;
	}
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo (void)
{
	char	str[1024];

	if (cls.demonum == -1)
		return;		// don't play demos

	SCR_BeginLoadingPlaque ();

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;
		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf ("No demos listed with startdemos\n");
			cls.demonum = -1;
			return;
		}
	}

	sprintf (str,"playdemo %s\n", cls.demos[cls.demonum]);
	Cbuf_InsertText (str);
	cls.demonum++;
}

/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f (void)
{
	entity_t	*ent;
	int			i;
	
	for (i=0,ent=cl_entities ; i<cl.num_entities ; i++,ent++)
	{
		Con_Printf ("%3i:",i);
		if (!ent->model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}
		Con_Printf ("%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n"
		,ent->model->name,ent->frame, ent->origin[0], ent->origin[1], ent->origin[2], ent->angles[0], ent->angles[1], ent->angles[2]);
	}
}


/*
===============
SetPal

Debugging tool, just flashes the screen
===============
*/
void SetPal (int i)
{
#if 0
	static int old;
	byte	pal[768];
	int		c;
	
	if (i == old)
		return;
	old = i;

	if (i==0)
		VID_SetPalette (host_basepal);
	else if (i==1)
	{
		for (c=0 ; c<768 ; c+=3)
		{
			pal[c] = 0;
			pal[c+1] = 255;
			pal[c+2] = 0;
		}
		VID_SetPalette (pal);
	}
	else
	{
		for (c=0 ; c<768 ; c+=3)
		{
			pal[c] = 0;
			pal[c+1] = 0;
			pal[c+2] = 255;
		}
		VID_SetPalette (pal);
	}
#endif
}

/*
===============
CL_AllocDlight

===============
*/
dlight_t *CL_AllocDlight (int key)
{
	int		i;
	dlight_t	*dl;

// first look for an exact key match
	if (key)
	{
		dl = cl_dlights;
		for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
		{
			if (dl->key == key)
			{
				memset (dl, 0, sizeof(*dl));
				dl->key = key;
				return dl;
			}
		}
	}

// then look for anything else
	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time)
		{
			memset (dl, 0, sizeof(*dl));
			dl->key = key;
			return dl;
		}
	}

	dl = &cl_dlights[0];
	memset (dl, 0, sizeof(*dl));
	dl->key = key;
	return dl;
}


/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights (void)
{
	int			i;
	dlight_t	*dl;
	float		time;
	
	time = cl.time - cl.oldtime;

	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time || !dl->radius)
			continue;
		
		dl->radius -= time*dl->decay;
		if (dl->radius < 0)
			dl->radius = 0;
	}
}


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
float	CL_LerpPoint (void)
{
	float	f, frac;

	f = cl.mtime[0] - cl.mtime[1];
	
	if (!f || cl_nolerp.value || cls.timedemo || sv.active)
	{
		cl.time = cl.mtime[0];
		return 1;
	}
		
	if (f > 0.1)
	{	// dropped packet, or start of demo
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f = 0.1;
	}
	frac = (cl.time - cl.mtime[1]) / f;
//Con_Printf ("frac: %f\n",frac);
	if (frac < 0)
	{
		if (frac < -0.01)
		{
SetPal(1);
			cl.time = cl.mtime[1];
//				Con_Printf ("low frac\n");
		}
		frac = 0;
	}
	else if (frac > 1)
	{
		if (frac > 1.01)
		{
SetPal(2);
			cl.time = cl.mtime[0];
//				Con_Printf ("high frac\n");
		}
		frac = 1;
	}
	else
		SetPal(0);
		
	return frac;
}


/*
===============
CL_RelinkEntities
===============
*/
void CL_RelinkEntities (void)
{
	entity_t	*ent;
	int			i, j;
	float		frac, f, d;
	vec3_t		delta;
	float		bobjrotate;
	vec3_t		oldorg;
	dlight_t	*dl;

// determine partial update time	
	frac = CL_LerpPoint ();

	cl_numvisedicts = 0;

//
// interpolate player info
//
	for (i=0 ; i<3 ; i++)
		cl.velocity[i] = cl.mvelocity[1][i] + 
			frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	if (cls.demoplayback)
	{
	// interpolate the angles	
		for (j=0 ; j<3 ; j++)
		{
			d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;
			cl.viewangles[j] = cl.mviewangles[1][j] + frac*d;
		}
	}
	
	bobjrotate = anglemod(100*cl.time);
	
// start on the entity after the world
	for (i=1,ent=cl_entities+1 ; i<cl.num_entities ; i++,ent++)
	{
		if (!ent->model)
		{	// empty slot
			if (ent->forcelink)
				R_RemoveEfrags (ent);	// just became empty
			continue;
		}

// if the object wasn't included in the last packet, remove it
		if (ent->msgtime != cl.mtime[0])
		{
			ent->model = NULL;
			continue;
		}

		VectorCopy (ent->origin, oldorg);

		if (ent->forcelink)
		{	// the entity was not updated in the last message
			// so move to the final spot
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->angles);
		}
		else
		{	// if the delta is large, assume a teleport and don't lerp
			f = frac;
			for (j=0 ; j<3 ; j++)
			{
				delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];
				if (delta[j] > 100 || delta[j] < -100)
					f = 1;		// assume a teleportation, not a motion
			}

		// interpolate the origin and angles
			for (j=0 ; j<3 ; j++)
			{
				ent->origin[j] = ent->msg_origins[1][j] + f*delta[j];

				d = ent->msg_angles[0][j] - ent->msg_angles[1][j];
				if (d > 180)
					d -= 360;
				else if (d < -180)
					d += 360;
				ent->angles[j] = ent->msg_angles[1][j] + f*d;
			}
			
		}

// rotate binary objects locally
		if (ent->model->flags & EF_ROTATE)
			ent->angles[1] = bobjrotate;

		if (ent->effects & EF_BRIGHTFIELD)
			R_EntityParticles (ent);
#ifdef QUAKE2
		if (ent->effects & EF_DARKFIELD)
			R_DarkFieldParticles (ent);
#endif
		if (ent->effects & EF_MUZZLEFLASH)
		{
			vec3_t		fv, rv, uv;

			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->origin[2] += 16;
			AngleVectors (ent->angles, fv, rv, uv);
			 
			VectorMA (dl->origin, 18, fv, dl->origin);
			dl->radius = 200 + (rand()&31);
			dl->minlight = 32;
			dl->die = cl.time + 0.1;
		}
		if (ent->effects & EF_BRIGHTLIGHT)
		{			
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->origin[2] += 16;
			dl->radius = 400 + (rand()&31);
			dl->die = cl.time + 0.001;
		}
		if (ent->effects & EF_DIMLIGHT)
		{			
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->radius = 200 + (rand()&31);
			dl->die = cl.time + 0.001;
		}
#ifdef QUAKE2
		if (ent->effects & EF_DARKLIGHT)
		{			
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->radius = 200.0 + (rand()&31);
			dl->die = cl.time + 0.001;
			dl->dark = true;
		}
		if (ent->effects & EF_LIGHT)
		{			
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->radius = 200;
			dl->die = cl.time + 0.001;
		}
#endif

		if (ent->model->flags & EF_GIB)
			R_RocketTrail (oldorg, ent->origin, 2);
		else if (ent->model->flags & EF_ZOMGIB)
			R_RocketTrail (oldorg, ent->origin, 4);
		else if (ent->model->flags & EF_TRACER)
			R_RocketTrail (oldorg, ent->origin, 3);
		else if (ent->model->flags & EF_TRACER2)
			R_RocketTrail (oldorg, ent->origin, 5);
		else if (ent->model->flags & EF_ROCKET)
		{
			R_RocketTrail (oldorg, ent->origin, 0);
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 200;
			dl->die = cl.time + 0.01;
		}
		else if (ent->model->flags & EF_GRENADE)
			R_RocketTrail (oldorg, ent->origin, 1);
		else if (ent->model->flags & EF_TRACER3)
			R_RocketTrail (oldorg, ent->origin, 6);

		ent->forcelink = false;

		if (i == cl.viewentity && !chase_active.value)
			continue;

#ifdef QUAKE2
		if ( ent->effects & EF_NODRAW )
			continue;
#endif
		if (cl_numvisedicts < MAX_VISEDICTS)
		{
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}

	/*
	// cl_main.c -> CL_RelinkEntities() 맨 아래 루프 수정
	for (i = 0; i < cl.num_statics; i++)
	{
		if (cl_static_entities[i].effects == 99)
		{
			// 천천히 회전
			cl_static_entities[i].angles[1] = anglemod(20.0f * cl.time);

			// 다중 프레임 모델이면 초당 10프레임으로 애니메이션 재생
			if (cl_static_entities[i].model && cl_static_entities[i].model->numframes > 1)
				cl_static_entities[i].frame = ((int)(cl.time * 10.0f)) % cl_static_entities[i].model->numframes;
		}
	} */

	//
	// 
	// cl_main.c -> CL_RelinkEntities() 맨 아래 루프 교체
	/*
	for (i = 0; i < cl.num_statics; i++)
	{
		if (cl_static_entities[i].effects == 99)
		{
			int sf = cl_static_anims[i].start_frame;
			int ef = cl_static_anims[i].end_frame;
			int count = ef - sf + 1;

			// 천천히 회전 (원치 않으면 이 줄 주석 처리)
			cl_static_entities[i].angles[1] = anglemod(20.0f * cl.time);

			if (count > 1)
				cl_static_entities[i].frame = sf + (((int)(cl.time * cl_static_anims[i].fps)) % count);
			else
				cl_static_entities[i].frame = sf;
		}
	}*/
	//
	// cl_main.c -> CL_RelinkEntities() 맨 아래 루프 교체
	for (i = 0; i < cl.num_statics; i++)
	{
		entity_t* ent = &cl_static_entities[i];
		static_anim_t* anim = &cl_static_anims[i];
		int count;
		qboolean is_moving = false;

		if (ent->effects != 99)
			continue;

		count = anim->end_frame - anim->start_frame + 1;

		if (anim->ai_mode == 0)
		{
			// 모드 0: 제자리 회전
			//ent->angles[1] = anglemod(20.0f * cl.time);
			// 모드 0: 지정된 속도로 제자리 회전
			ent->angles[1] = anglemod(ent->angles[1] + anim->rot_speed * (float)host_frametime);
			is_moving = true;
			is_moving = true;
		}
		else if (anim->ai_mode == 1 || anim->ai_mode == 2 || anim->ai_mode == 4)
		{
			vec3_t dir;
			float dist, stop_dist;

			if (anim->ai_mode == 4)
			{
				// 목표 좌표 방향 계산
				dir[0] = anim->target_x - ent->origin[0];
				dir[1] = anim->target_y - ent->origin[1];
				dir[2] = 0;
				stop_dist = 10.0f;
			}
			else
			{
				// 플레이어 방향 계산
				VectorSubtract(r_refdef.vieworg, ent->origin, dir);
				stop_dist = 80.0f;
			}

			dist = (float)sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
			if (dist > 1.0f)
				ent->angles[1] = (float)(atan2(dir[1], dir[0]) * (180.0 / M_PI));

			// 모드 2(따라오기) 또는 모드 4(좌표 이동)
			if ((anim->ai_mode == 2 || anim->ai_mode == 4) && dist > stop_dist && sv.active)
			{
				vec3_t step_start, step_end, floor_end;
				trace_t tr;

				VectorCopy(ent->origin, step_start);
				step_start[2] += 24.0f;

				VectorCopy(step_start, step_end);
				step_end[0] += (dir[0] / dist) * anim->speed * (float)host_frametime;
				step_end[1] += (dir[1] / dist) * anim->speed * (float)host_frametime;

				tr = SV_Move(step_start, vec3_origin, vec3_origin, step_end, MOVE_NOMONSTERS, sv.edicts);
				
				if (tr.fraction == 1.0f)
				{
					VectorCopy(step_end, floor_end);
					floor_end[2] -= 80.0f;
					tr = SV_Move(step_end, vec3_origin, vec3_origin, floor_end, MOVE_NOMONSTERS, sv.edicts);

					if (tr.fraction < 1.0f)
					{
						VectorCopy(tr.endpos, ent->origin);
						ent->origin[2] -= anim->min_z;
						is_moving = true;
					}
				} 
				//
				// cl_main.c -> CL_RelinkEntities() 내부 바닥 높이 적용 부분 교체
				if (tr.fraction < 1.0f)
				{
					float min_z = anim->min_z;

					// min_z가 0이면 모델 헤더에서 실제 하단 Z좌표를 직접 가져와 보정
					if (min_z == 0.0f && ent->model)
					{
						min_z = ent->model->mins[2];
						if (ent->model->type == mod_alias)
						{
							aliashdr_t* hdr = (aliashdr_t*)Mod_Extradata(ent->model);
							if (hdr)
								min_z = hdr->scale_origin[2];
						}
						anim->min_z = min_z;
					}

					VectorCopy(tr.endpos, ent->origin);
					ent->origin[2] -= min_z;
					is_moving = true;
				}

				//

			}
			else if (anim->ai_mode == 4 && dist <= stop_dist)
			{
				anim->ai_mode = 3; // 목표 도착 시 자동 정지
			}
		}
		// 모드 3(정지)은 위치/각도 유지

		if (count > 1 && (anim->ai_mode == 0 || anim->ai_mode == 1 || is_moving))
			ent->frame = anim->start_frame + (((int)(cl.time * anim->fps)) % count);
		else
			ent->frame = anim->start_frame;

		if (cl_numvisedicts < MAX_VISEDICTS)
		{
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}
	//
}


/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer (void)
{
	int		ret;

	cl.oldtime = cl.time;
	cl.time += host_frametime;
	
	do
	{
		ret = CL_GetMessage ();
		if (ret == -1)
			Host_Error ("CL_ReadFromServer: lost server connection");
		if (!ret)
			break;
		
		cl.last_received_message = realtime;
		CL_ParseServerMessage ();
	} while (ret && cls.state == ca_connected);
	
	if (cl_shownet.value)
		Con_Printf ("\n");

	CL_RelinkEntities ();
	CL_UpdateTEnts ();

//
// bring the links up to date
//
	return 0;
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd (void)
{
	usercmd_t		cmd;

	if (cls.state != ca_connected)
		return;

	if (cls.signon == SIGNONS)
	{
	// get basic movement from keyboard
		CL_BaseMove (&cmd);
	
	// allow mice or other external controllers to add to the move
		IN_Move (&cmd);
	
	// send the unreliable message
		CL_SendMove (&cmd);
	
	}

	if (cls.demoplayback)
	{
		SZ_Clear (&cls.message);
		return;
	}
	
// send the reliable message
	if (!cls.message.cursize)
		return;		// no message at all
	
	if (!NET_CanSendMessage (cls.netcon))
	{
		Con_DPrintf ("CL_WriteToServer: can't send\n");
		return;
	}

	if (NET_SendMessage (cls.netcon, &cls.message) == -1)
		Host_Error ("CL_WriteToServer: lost server connection");

	SZ_Clear (&cls.message);
}

//


// cl_main.c 에 추가
// cl_main.c -> CL_SpawnModel_f() 교체
// cl_main.c -> CL_SpawnModel_f() 안전 버전으로 교체
void CL_SpawnModel_f(void)
{
	model_t* mod;
	entity_t* ent;
	vec3_t    forward, right, up;
	vec3_t    start, end;
	trace_t   trace;
	char* modelname;
	int       idx, total_frames, start_f, end_f, ai_mode;

	if (cls.state != ca_connected || !cl.worldmodel)
	{
		Con_Printf("Map is not loaded.\n");
		return;
	}

	if (Cmd_Argc() < 2)
	{
		Con_Printf("Usage: spawnmdl <path> [start_frame] [end_frame]\n");
		return;
	}

	if (cl.num_statics >= MAX_STATIC_ENTITIES)
	{
		Con_Printf("Max static entities reached.\n");
		return;
	}

	modelname = Cmd_Argv(1);
	mod = Mod_ForName(modelname, false);
	if (!mod)
	{
		Con_Printf("Model not found: %s\n", modelname);
		return;
	}

	idx = cl.num_statics;
	ent = &cl_static_entities[idx];
	memset(ent, 0, sizeof(*ent));
	cl.num_statics++;

	ent->model = mod;
	ent->colormap = vid.colormap;
	ent->skinnum = 0;
	ent->effects = 99;

	// 1. 플레이어 전방 100유닛 위치 계산
	AngleVectors(cl.viewangles, forward, right, up);
	VectorMA(r_refdef.vieworg, 100.0f, forward, start);
	VectorCopy(start, ent->origin);

	// 2. 바닥 방향(-1024)으로 안전하게 레이캐스트 (SV_Move 사용)
	if (sv.active)
	{
		VectorCopy(start, end);
		end[2] -= 1024.0f;
		trace = SV_Move(start, vec3_origin, vec3_origin, end, MOVE_NOMONSTERS, sv.edicts);

		if (trace.fraction < 1.0f)
		{
			VectorCopy(trace.endpos, ent->origin);
			ent->origin[2] -= mod->mins[2];
		}
	}
	//
	// cl_main.c -> CL_SpawnModel_f() 내부
	if (sv.active)
	{
		VectorCopy(start, end);
		end[2] -= 1024.0f;
		trace = SV_Move(start, vec3_origin, vec3_origin, end, MOVE_NOMONSTERS, sv.edicts);

		if (trace.fraction < 1.0f)
		{
			float min_z = mod->mins[2];

			// Alias 모델(IDPO/MDL3/MDL7)은 헤더의 실제 최저 Z좌표(scale_origin[2]) 사용
			if (mod->type == mod_alias)
			{
				aliashdr_t* hdr = (aliashdr_t*)Mod_Extradata(mod);
				if (hdr)
					min_z = hdr->scale_origin[2];
			}

			VectorCopy(trace.endpos, ent->origin);
			ent->origin[2] -= min_z;
		}
	}
	//
	ent->angles[0] = 0;
	ent->angles[1] = cl.viewangles[1] + 180.0f;
	ent->angles[2] = 0;

	// 3. 음수 프레임 크래시 방지 (최소 1프레임 보장)
	total_frames = (mod->numframes > 0) ? mod->numframes : 1;
	start_f = (Cmd_Argc() >= 3) ? atoi(Cmd_Argv(2)) : 0;
	end_f = (Cmd_Argc() >= 4) ? atoi(Cmd_Argv(3)) : (total_frames - 1);

	if (start_f < 0) start_f = 0;
	if (start_f >= total_frames) start_f = total_frames - 1;
	if (end_f < start_f) end_f = start_f;
	if (end_f >= total_frames) end_f = total_frames - 1;

	cl_static_anims[idx].start_frame = start_f;
	cl_static_anims[idx].end_frame = end_f;
	cl_static_anims[idx].fps = 10.0f;
	ent->frame = start_f;

	R_AddEfrags(ent);
	Con_Printf("Spawned %s (frames %d..%d / total %d)\n",
		modelname, start_f, end_f, total_frames);
	cl_static_anims[idx].speed = 65.0f;
	cl_static_anims[idx].rot_speed = 20.0f;
	Con_Printf("Spawned [ID:%d] %s (frames %d..%d, ai:%d)\n",
		idx, modelname, start_f, end_f, ai_mode);
}

// cl_main.c -> CL_SpawnModel_f() 아래에 추가
void CL_ModelControl_f(void)
{
	int idx;
	char* action;
	entity_t* ent;
	static_anim_t* anim;

	if (Cmd_Argc() < 3)
	{
		Con_Printf("Usage: mdl <id|-1> <mode|anim|speed|move|forward|del> [args...]\n");
		return;
	}

	idx = atoi(Cmd_Argv(1));
	if (idx < 0)
		idx = cl.num_statics - 1; // -1이면 마지막으로 소환한 모델

	if (idx < 0 || idx >= cl.num_statics || cl_static_entities[idx].effects != 99)
	{
		Con_Printf("mdl: Invalid model ID %d\n", idx);
		return;
	}

	ent = &cl_static_entities[idx];
	anim = &cl_static_anims[idx];
	action = Cmd_Argv(2);

	if (!Q_strcasecmp(action, "mode") && Cmd_Argc() >= 4)
	{
		// 0:회전, 1:바라보기, 2:따라오기, 3:정지
		anim->ai_mode = atoi(Cmd_Argv(3));
	}
	else if (!Q_strcasecmp(action, "anim") && Cmd_Argc() >= 5)
	{
		// mdl <id> anim <start> <end> [fps]
		int total = (ent->model && ent->model->numframes > 0) ? ent->model->numframes : 1;
		int sf = atoi(Cmd_Argv(3));
		int ef = atoi(Cmd_Argv(4));

		if (sf < 0) sf = 0;
		if (sf >= total) sf = total - 1;
		if (ef < sf) ef = sf;
		if (ef >= total) ef = total - 1;

		anim->start_frame = sf;
		anim->end_frame = ef;
		if (Cmd_Argc() >= 6)
			anim->fps = (float)atof(Cmd_Argv(5));
	}
	else if (!Q_strcasecmp(action, "speed") && Cmd_Argc() >= 4)
	{
		anim->speed = (float)atof(Cmd_Argv(3));
	}
	else if (!Q_strcasecmp(action, "move") && Cmd_Argc() >= 5)
	{
		// 특정 월드 좌표 (X, Y)로 걸어가기
		anim->target_x = (float)atof(Cmd_Argv(3));
		anim->target_y = (float)atof(Cmd_Argv(4));
		anim->ai_mode = 4;
	}
	else if (!Q_strcasecmp(action, "forward") && Cmd_Argc() >= 4)
	{
		// 현재 바라보는 방향으로 dist만큼 걸어가기
		float dist = (float)atof(Cmd_Argv(3));
		float rad = ent->angles[1] * (float)(M_PI / 180.0);
		anim->target_x = ent->origin[0] + (float)cos(rad) * dist;
		anim->target_y = ent->origin[1] + (float)sin(rad) * dist;
		anim->ai_mode = 4;
	}
	else if (!Q_strcasecmp(action, "del"))
	{
		// 모델 비활성화(삭제)
		ent->effects = 0;
	}
	// cl_main.c -> CL_ModelControl_f() 내부 (else if 체인에 추가)
	else if (!Q_strcasecmp(action, "pos") && Cmd_Argc() >= 6)
	{
		// 절대 3D 좌표로 이동: mdl <id> pos <x> <y> <z>
		ent->origin[0] = (float)atof(Cmd_Argv(3));
		ent->origin[1] = (float)atof(Cmd_Argv(4));
		ent->origin[2] = (float)atof(Cmd_Argv(5));
	}
	else if (!Q_strcasecmp(action, "addpos") && Cmd_Argc() >= 6)
	{
		// 현재 위치에서 상대적으로 이동: mdl <id> addpos <dx> <dy> <dz>
		ent->origin[0] += (float)atof(Cmd_Argv(3));
		ent->origin[1] += (float)atof(Cmd_Argv(4));
		ent->origin[2] += (float)atof(Cmd_Argv(5));
	}
	// cl_main.c -> CL_ModelControl_f() 내부에 추가
	else if (!Q_strcasecmp(action, "rotspeed") && Cmd_Argc() >= 4)
	{
		// 회전 속도 설정: mdl <id> rotspeed <초당 각도> (음수면 반대 방향)
		anim->rot_speed = (float)atof(Cmd_Argv(3));
	}
	else if (!Q_strcasecmp(action, "tilt") && Cmd_Argc() >= 5)
	{
		// 기울기 설정: mdl <id> tilt <pitch(앞뒤)> <roll(좌우)>
		ent->angles[0] = (float)atof(Cmd_Argv(3));
		ent->angles[2] = (float)atof(Cmd_Argv(4));
	}
}


void CL_SpawnModel_f0(void)
{
	model_t* mod;
	entity_t* ent;
	vec3_t    forward, right, up;
	char* modelname;

	if (cls.state != ca_connected || !cl.worldmodel)
	{
		Con_Printf("맵이 로드된 상태에서만 사용할 수 있습니다.\n");
		return;
	}

	if (Cmd_Argc() < 2)
	{
		Con_Printf("사용법: spawnmdl <모델경로> (예: spawnmdl progs/earth.mdl)\n");
		return;
	}

	if (cl.num_statics >= MAX_STATIC_ENTITIES)
	{
		Con_Printf("더 이상 정적 모델을 소환할 수 없습니다.\n");
		return;
	}

	modelname = Cmd_Argv(1);

	// 1. 모델 로딩 (MDL3/4/5/7 및 기본 Quake MDL 모두 자동 인식)
	mod = Mod_ForName(modelname, false);
	if (!mod)
	{
		Con_Printf("Model not found: %s (gamedir: %s)\n", modelname, com_gamedir);
		return;
	}

	// 2. 정적 엔티티 슬롯 할당 및 초기화
	ent = &cl_static_entities[cl.num_statics];
	memset(ent, 0, sizeof(*ent));
	cl.num_statics++;

	ent->model = mod;
	ent->frame = 0;
	ent->colormap = vid.colormap;
	ent->skinnum = 0;
	ent->effects = 99; // 자전(회전) 대상 표시용 커스텀 값

	// 3. 플레이어가 바라보는 방향 100 유닛 앞에 배치
	AngleVectors(cl.viewangles, forward, right, up);
	VectorMA(r_refdef.vieworg, 100.0f, forward, ent->origin);

	// 모델이 플레이어를 향하도록 각도 설정 (선택)
	ent->angles[0] = 0;
	ent->angles[1] = cl.viewangles[1] + 180.0f;
	ent->angles[2] = 0;

	// 4. 현재 맵의 BSP 리프에 엔티티 등록 (화면에 그려지도록 함)
	R_AddEfrags(ent);

	Con_Printf("Spawned model: %s\n", modelname);
}

void CL_ModelList_f(void)
{
	int i;
	for (i = 0; i < cl.num_statics; i++)
	{
		if (cl_static_entities[i].effects == 99 && cl_static_entities[i].model)
		{
			Con_Printf("ID:%d | %s | mode:%d | pos:(%.0f, %.0f, %.0f)\n",
				i,
				cl_static_entities[i].model->name,
				cl_static_anims[i].ai_mode,
				cl_static_entities[i].origin[0],
				cl_static_entities[i].origin[1],
				cl_static_entities[i].origin[2]);
		}
	}
}

//
/*
=================
CL_Init
=================
*/
void CL_Init (void)
{	
	SZ_Alloc (&cls.message, 1024);

	CL_InitInput ();
	CL_InitTEnts ();
	
//
// register our commands
//
	Cvar_RegisterVariable (&cl_name);
	Cvar_RegisterVariable (&cl_color);
	Cvar_RegisterVariable (&cl_upspeed);
	Cvar_RegisterVariable (&cl_forwardspeed);
	Cvar_RegisterVariable (&cl_backspeed);
	Cvar_RegisterVariable (&cl_sidespeed);
	Cvar_RegisterVariable (&cl_movespeedkey);
	Cvar_RegisterVariable (&cl_yawspeed);
	Cvar_RegisterVariable (&cl_pitchspeed);
	Cvar_RegisterVariable (&cl_anglespeedkey);
	Cvar_RegisterVariable (&cl_shownet);
	Cvar_RegisterVariable (&cl_nolerp);
	Cvar_RegisterVariable (&lookspring);
	Cvar_RegisterVariable (&lookstrafe);
	Cvar_RegisterVariable (&sensitivity);

	Cvar_RegisterVariable (&m_pitch);
	Cvar_RegisterVariable (&m_yaw);
	Cvar_RegisterVariable (&m_forward);
	Cvar_RegisterVariable (&m_side);

//	Cvar_RegisterVariable (&cl_autofire);
	
	Cmd_AddCommand ("entities", CL_PrintEntities_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("stop", CL_Stop_f);
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f);
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f);
	// cl_main.c -> CL_Init() 내부
	Cmd_AddCommand("spawnmdl", CL_SpawnModel_f);
	Cmd_AddCommand("mdl", CL_ModelControl_f);
	Cmd_AddCommand("mdllist", CL_ModelList_f);
}

// cl_main.c 맨 아래에 추가
__declspec(dllexport) int __stdcall VB_QuakeGetPlayerPos(float* x, float* y, float* z, float* yaw)
{
	if (cls.state != ca_connected)
		return 0;

	if (x)   *x = r_refdef.vieworg[0];
	if (y)   *y = r_refdef.vieworg[1];
	if (z)   *z = r_refdef.vieworg[2];
	if (yaw) *yaw = cl.viewangles[1];
	return 1;
}

__declspec(dllexport) int __stdcall VB_QuakeGetModelPos(int idx, float* x, float* y, float* z, float* yaw)
{
	entity_t* ent;

	if (cls.state != ca_connected)
		return 0;

	if (idx < 0)
		idx = cl.num_statics - 1;

	if (idx < 0 || idx >= cl.num_statics || cl_static_entities[idx].effects != 99)
		return 0;

	ent = &cl_static_entities[idx];
	if (x)   *x = ent->origin[0];
	if (y)   *y = ent->origin[1];
	if (z)   *z = ent->origin[2];
	if (yaw) *yaw = ent->angles[1];
	return 1;
}