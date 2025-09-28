cd .. 
rm -rf sdkconfig build
export IDF_TARGET=esp32s3
cd scripts
python release.py  xiaozhiyunliao-s3  --lang cn
python release.py  xiaozhiyunliao-s3  --lang tw --skip-set-target
python release.py  xiaozhiyunliao-s3  --lang en --skip-set-target
python release.py  xiaozhiyunliao-s3  --lang jp --skip-set-target
#python release.py  xiaozhiyunliao-c3  --lang cn
#python release.py  xiaozhiyunliao-c3  --lang tw --skip-set-target
