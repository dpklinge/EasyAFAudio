"""Prepare pinned CPU inference assets for an offline Intent build."""
from argparse import ArgumentParser
from concurrent.futures import ThreadPoolExecutor
from hashlib import file_digest
from pathlib import Path
from shutil import copyfileobj
from urllib.request import urlopen
from zipfile import ZipFile

REVISION = "1110a243fdf4706b3f48f1d95db1a4f5529b4d41"
MODEL_SITE = f"https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2/resolve/{REVISION}"
ORT_URL = ("https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/1.23.2/"
           "microsoft.ml.onnxruntime.1.23.2.nupkg")


def download(url, target, expected):
    if target.is_file():
        with target.open("rb") as existing:
            if file_digest(existing, "sha256").hexdigest() == expected:
                return
    temporary = target.with_suffix(target.suffix + ".partial")
    with urlopen(url, timeout=60) as response, temporary.open("wb") as output:
        copyfileobj(response, output)
    with temporary.open("rb") as output:
        if file_digest(output, "sha256").hexdigest() != expected:
            raise ValueError(f"Checksum mismatch for {target.name}")
    temporary.replace(target)


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    root = args.directory
    model = root / "model"
    include = root / "include"
    for directory in (include, model):
        directory.mkdir(parents=True, exist_ok=True)
    package = root / "onnxruntime.nupkg"
    files = (
        (f"{MODEL_SITE}/onnx/model.onnx", model / "model.onnx",
         "6fd5d72fe4589f189f8ebc006442dbb529bb7ce38f8082112682524616046452"),
        (f"{MODEL_SITE}/vocab.txt", model / "vocab.txt",
         "07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3"),
        ("https://www.apache.org/licenses/LICENSE-2.0.txt", model / "MiniLM-LICENSE.txt",
         "cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30"),
        (ORT_URL, package, "25fe172f7fcdf34f2b5b02c8b997f6b88abc282956849d4af4335af23d0c4e4a"),
    )
    with ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(lambda entry: download(*entry), files))

    with ZipFile(package) as archive:
        paths = {
            member: include / Path(member).name
            for member in archive.namelist()
            if member.startswith("build/native/include/") and member.endswith(".h")
        }
        paths.update({
            "runtimes/win-x64/native/onnxruntime.dll": model / "onnxruntime.dll",
            "LICENSE": model / "ONNX-Runtime-LICENSE.txt",
            "ThirdPartyNotices.txt": model / "ONNX-Runtime-ThirdPartyNotices.txt",
        })
        for member, target in paths.items():
            content = archive.read(member)
            if not target.exists() or target.read_bytes() != content:
                target.write_bytes(content)
    print(f"Verified full-precision MiniLM and ONNX Runtime assets in {root.resolve()}")


if __name__ == "__main__":
    main()
