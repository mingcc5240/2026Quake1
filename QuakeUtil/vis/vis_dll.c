// vis_dll.c
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

extern jmp_buf vis_err_jmp;
extern char    vis_err_msg[512];
extern int     vis_main (int argc, char **argv);

__declspec(dllexport) int VIS_ComputeToMemory (
	const unsigned char *in_bsp,
	int in_size,
	unsigned char **out_bsp,
	int *out_size,
	char *err_buf,
	int err_buf_size)
{
	char temp_path[MAX_PATH];
	char *argv[2];
	FILE *fp;
	long fsize;
	unsigned char *buf;

	*out_bsp = NULL;
	*out_size = 0;
	if (err_buf && err_buf_size > 0) err_buf[0] = 0;

	if (!in_bsp || in_size <= 0)
	{
		if (err_buf && err_buf_size > 0)
			strncpy (err_buf, "Invalid input BSP buffer", err_buf_size - 1);
		return 0;
	}

	GetTempPathA (MAX_PATH, temp_path);
	strcat (temp_path, "winquake_vis_temp.bsp");

	fp = fopen (temp_path, "wb");
	if (!fp)
	{
		if (err_buf && err_buf_size > 0)
			strncpy (err_buf, "Failed to write temp BSP for vis", err_buf_size - 1);
		return 0;
	}
	fwrite (in_bsp, 1, in_size, fp);
	fclose (fp);

	argv[0] = "vis";
	argv[1] = temp_path;

	if (setjmp (vis_err_jmp) != 0)
	{
		if (err_buf && err_buf_size > 0)
		{
			strncpy (err_buf, vis_err_msg, err_buf_size - 1);
			err_buf[err_buf_size - 1] = 0;
		}
		DeleteFileA (temp_path);
		return 0;
	}

	vis_main (2, argv);

	fp = fopen (temp_path, "rb");
	if (!fp)
	{
		DeleteFileA (temp_path);
		return 0;
	}

	fseek (fp, 0, SEEK_END);
	fsize = ftell (fp);
	fseek (fp, 0, SEEK_SET);

	buf = (unsigned char *)malloc (fsize);
	if (!buf)
	{
		fclose (fp);
		DeleteFileA (temp_path);
		return 0;
	}

	fread (buf, 1, fsize, fp);
	fclose (fp);
	DeleteFileA (temp_path);

	*out_bsp = buf;
	*out_size = (int)fsize;
	return 1;
}

__declspec(dllexport) void VIS_FreeBuffer (unsigned char *buf)
{
	if (buf) free (buf);
}