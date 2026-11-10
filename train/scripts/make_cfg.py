# Write a PaddleOCR training config for korean_PP-OCRv5_mobile_rec fine-tuning.
# usage: make_cfg.py <epochs> <lr> <out.yml>
# env (all optional): RUN (ckpt/<RUN>), LIST (data/train/<LIST>), PRE (pretrained prefix), EVAL_EVERY,
#   HWR_AMP=0|1, HWR_AMP_DTYPE=bfloat16|float16, HWR_AMP_LEVEL=O1|O2, WIDTH (image width, default 960), BATCH.
import yaml, sys, os
W = os.environ.get('HWR_WORK', os.getcwd())   # training work dir (see train/README.md)
e = os.environ.get
epochs, lr, out = int(sys.argv[1]), float(sys.argv[2]), sys.argv[3]
width = int(e('WIDTH', 960))
batch = int(e('BATCH', 64))
c = yaml.safe_load(open(f'{W}/PaddleOCR/configs/rec/PP-OCRv5/multi_language/korean_PP-OCRv5_mobile_rec.yml'))
g = c['Global']
g.update(epoch_num=epochs, save_model_dir=f'{W}/ckpt/' + e('RUN', 'run1'), save_epoch_step=1,
         eval_batch_step=[0, int(e('EVAL_EVERY', 4000))],
         pretrained_model=e('PRE', f'{W}/pretrained/korean'), character_dict_path=f'{W}/data/dict.txt', max_text_length=40,
         distributed=False, use_amp=e('HWR_AMP', '0') == '1', amp_level=e('HWR_AMP_LEVEL', 'O1'),
         amp_dtype=e('HWR_AMP_DTYPE', 'float16'), scale_loss=1.0 if e('HWR_AMP_DTYPE', 'float16') == 'bfloat16' else 1024.0,
         use_dynamic_loss_scaling=e('HWR_AMP_DTYPE', 'float16') != 'bfloat16',
         d2s_train_image_shape=[3, 48, width], print_batch_step=50)
c['Optimizer']['lr'].update(learning_rate=lr, warmup_epoch=0.3)
ml = 40
c['Architecture']['Head']['head_list'][1]['NRTRHead']['max_text_length'] = ml
rs = {'RecResizeImg': {'image_shape': [3, 48, width]}}
ks = {'KeepKeys': {'keep_keys': ['image', 'label_ctc', 'label_gtc', 'length', 'valid_ratio']}}
dec = {'DecodeImage': {'img_mode': 'BGR', 'channel_first': False}}
enc = {'MultiLabelEncode': {'gtc_encode': 'NRTRLabelEncode', 'max_text_length': ml}}
c['Train']['dataset'] = {'name': 'SimpleDataSet', 'data_dir': f'{W}/data/train',
                         'label_file_list': [f'{W}/data/train/' + e('LIST', 'train_list.txt')],
                         'transforms': [dec, enc, rs, ks]}
c['Train']['loader'] = {'shuffle': True, 'batch_size_per_card': batch, 'drop_last': True, 'num_workers': int(e('WORKERS', 8))}
c['Train'].pop('sampler', None)
c['Eval']['dataset'] = {'name': 'SimpleDataSet', 'data_dir': f'{W}/data/val', 'label_file_list': [f'{W}/data/val/val_list.txt'],
                        'transforms': [dec, enc, rs, ks]}
c['Eval']['loader'].update(batch_size_per_card=batch, num_workers=4)
yaml.safe_dump(c, open(out, 'w'), allow_unicode=True)
print(f'wrote {out}: epochs={epochs} lr={lr} width={width} amp={g["use_amp"]}/{g["amp_dtype"]}/{g["amp_level"]} pre={g["pretrained_model"]}')
