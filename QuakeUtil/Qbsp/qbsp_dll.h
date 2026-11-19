#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// .map 파일 경로를 받아 컴파일한 뒤, 완성된 BSP29 바이너리를 메모리 버퍼로 반환
// 반환값: 1 = 성공, 0 = 실패 (err_buf에 사유 기록)
typedef int (*PFN_QBSP_COMPILE)(
	const char *map_path,
	const char* wad_path, // [추가] WinQuake에서 넘겨주는 정확한 base.wad 경로
	unsigned char **out_bsp,
	int *out_size,
	char *err_buf,
	int err_buf_size
);

typedef void (*PFN_QBSP_FREE)(unsigned char *buf);

#ifdef __cplusplus
}
#endif