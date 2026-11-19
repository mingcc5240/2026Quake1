import os
import struct
import sys


def extract_pak(pak_path, output_dir):
    if not os.path.exists(pak_path):
        print(f"오류: '{pak_path}' 파일을 찾을 수 없습니다.")
        return

    with open(pak_path, "rb") as f:
        # 1. PAK 헤더 읽기 (12 바이트: 4s = 4바이트 문자열, ii = 4바이트 부호있는 정수 2개)
        header_data = f.read(12)
        if len(header_data) < 12:
            print("오류: 파일 크기가 너무 작습니다.")
            return

        magic, dir_offset, dir_length = struct.unpack("<4sii", header_data)

        if magic != b"PACK":
            print(f"오류: 올바른 Quake PAK 파일이 아닙니다. (Magic: {magic})")
            return

        # 2. 디렉토리 엔트리 개수 계산 (각 엔트리는 64바이트)
        num_entries = dir_length // 64
        print(f"'{pak_path}' 분석 완료: 총 {num_entries}개의 파일 발견.")

        # 3. 파일 목록 위치로 이동하여 엔트리 정보 모두 읽기
        f.seek(dir_offset)
        entries = []
        for _ in range(num_entries):
            entry_data = f.read(64)
            # 56s = 56바이트 파일명, ii = 파일 오프셋(4B), 파일 크기(4B)
            raw_name, file_offset, file_size = struct.unpack(
                "<56sii", entry_data
            )
            # 널 문자(\x00) 제거 후 문자열 디코딩
            file_name = raw_name.split(b"\x00", 1)[0].decode(
                "ascii", errors="ignore"
            )
            entries.append((file_name, file_offset, file_size))

        # 4. 각 파일 추출 및 저장
        for idx, (file_name, file_offset, file_size) in enumerate(entries, 1):
            # 경로 구분자 정규화 및 대상 경로 생성
            safe_rel_path = os.path.normpath(file_name.replace("/", os.sep))
            out_file_path = os.path.join(output_dir, safe_rel_path)

            # 상위 폴더가 없으면 생성
            os.makedirs(os.path.dirname(out_file_path), exist_ok=True)

            # PAK 내부에서 해당 파일 데이터 읽어서 쓰기
            f.seek(file_offset)
            file_data = f.read(file_size)

            with open(out_file_path, "wb") as out_f:
                out_f.write(file_data)

            print(
                f"[{idx}/{num_entries}] 추출됨: {file_name} ({file_size:,} bytes)"
            )

    print(f"\n추출 완료! 저장 경로: {os.path.abspath(output_dir)}")


if __name__ == "__main__":
    # 명령줄 인자로 실행하거나 기본값(PAK0.PAK) 사용
    if len(sys.argv) >= 2:
        target_pak = sys.argv[1]
        target_out = sys.argv[2] if len(sys.argv) >= 3 else "extracted_pak"
    else:
        # 직접 경로를 지정해서 실행할 경우 아래 경로를 수정하세요.
        target_pak = "id1/PAK0.PAK"
        target_out = "id1_extracted"

    extract_pak(target_pak, target_out)