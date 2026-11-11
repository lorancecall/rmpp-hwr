# rmpp-hwr Korean recognition model

The default Korean model in rmpp-hwr releases (`rec.onnx`, `dict.txt`, `model.txt`; on the tablet in
`/home/root/.local/share/rmpp-hwr/` and `models/<version>/`). Each release ships the latest model trained on the
rmpp-hwr-db data; older versions are not kept on the tablet.

| | |
|---|---|
| Current version | `rmpp-hwr-korean-v0.3` (training run fix4) |
| Task | Line recognition of handwriting (CTC), Korean with English and digits, 11,945 characters |
| Architecture | PaddleOCR PP-OCRv5 mobile recognition (SVTR-LCNet), input 3 x 48 x W, ONNX opset 14 |
| Error | about 2 % character error rate on the held-out lines of the rmpp-hwr-db data |
| License of the weights | **CC BY-SA 4.0** (see "Licenses" below) |

## Training data

1. **Base weights**: PaddleOCR `korean_PP-OCRv5_mobile_rec`. Copyright PaddlePaddle Authors, Apache License 2.0,
   https://github.com/PaddlePaddle/PaddleOCR.
2. **rmpp-hwr-db**: handwritten practice lines contributed by its writers on the reMarkable Paper Pro,
   CC BY 4.0, https://github.com/lorancecall/rmpp-hwr-db (contributors consent to model training and release).
3. **Synthetic lines**: text rendered with handwriting and print fonts from Google Fonts (SIL Open Font License
   1.1); rendering text for training is permitted by the OFL, no font is redistributed.
4. **korean-handwriting-strokes** by jbaudanza (Hugging Face), CC BY-SA 4.0: Korean pen recordings rendered as
   lines. Attribution required; the model is released under CC BY-SA 4.0 so that its share-alike terms are met.
5. **Text corpora** for the synthetic lines (`en.parquet`, `ko.parquet`, `sentences.parquet`): sentences, origin
   to be confirmed before each release (if Wikipedia: CC BY-SA, compatible with the license above).

Not used: IAM-OnDB (its terms do not allow redistributing models trained on it).

## Licenses

The weights are a derivative of the Apache-2.0 base model and are trained on CC BY 4.0 and CC BY-SA 4.0 data, so
they are released under CC BY-SA 4.0 with the attributions above. The rmpp-hwr code stays under its own license
(see LICENSE); the language packs for Chinese, Japanese, English and Latin scripts are unmodified PaddleOCR models
converted to ONNX, Apache License 2.0 (see NOTICE).

## Limits

Trained mostly on a few writers' handwriting: other hands, very cursive writing and rare characters are less
accurate. Lines are recognized one at a time; layout (columns, tables) is not understood.
