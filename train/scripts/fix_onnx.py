# usage: fix_onnx.py in.onnx out.onnx   -> dynamic N and W, plain dim names, input 'x'
#        fix_onnx.py --show a.onnx b.onnx   (print io)
import sys, onnx
def io(m):
    for t in list(m.graph.input) + list(m.graph.output):
        print(' ', t.name, [d.dim_param or d.dim_value for d in t.type.tensor_type.shape.dim])
if sys.argv[1] == '--show':
    for p in sys.argv[2:]:
        m = onnx.load(p); print(p, m.opset_import[0].version); io(m); print('  last ops', [n.op_type for n in m.graph.node[-3:]])
    sys.exit()
m = onnx.load(sys.argv[1]); print('before'); io(m)
nm = ['N', 3, 48, 'W']
for t in m.graph.input:
    t.name if False else None
    for i, d in enumerate(t.type.tensor_type.shape.dim):
        d.ClearField('dim_value'); d.ClearField('dim_param')
        if isinstance(nm[i], str): d.dim_param = nm[i]
        else: d.dim_value = nm[i]
for t in m.graph.output:
    for i, d in enumerate(t.type.tensor_type.shape.dim):
        d.ClearField('dim_value'); d.ClearField('dim_param'); d.dim_param = ['N', 'T', 'C'][i]
for vi in list(m.graph.value_info): m.graph.value_info.remove(vi)
onnx.checker.check_model(m)
onnx.save(m, sys.argv[2]); print('after'); io(m)
