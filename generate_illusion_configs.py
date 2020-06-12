import os
import argparse
import json
import math

SCHEDULE_DIR = "/rsgs/pool0/yunfengxin/accel-sim-v2/yunfeng/multichip/multichip_message_passing_32"
OUTPUT_DIR = "./illusion_configs"
NETS = ["alex_net","resnet50","vgg_net"]
CONFIGS = [0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048]

def generate_config(nodes, schedule, topo, route, k, n, fname):
    return \
'''topology = %s;
k = %d;
n = %d;
// Routing
routing_function = %s;
// Flow control
num_vcs = 2;
// Traffic
traffic = illusion;
traffic_schedule = {%s};
latency_thres = 10000.0;
//sample_period = 10000; 

sim_power=1;
tech_file = ../src/power/techfile.txt;
power_output_file = power_%s.txt;

''' %(topo,k,n,route,schedule,fname)

def main(net, word=16, batch=1):
    output = {}
    NETWORK_NAME = net + "_" + str(word) + "_" + str(batch)
    filename = "%s/%s_2048.0_mp.csv" %(os.path.join(SCHEDULE_DIR, NETWORK_NAME, "2048"), net)
    layer_list = []
    fi = open(os.path.join(filename), "r")
    lines = fi.read().splitlines()
    for line in lines[1:]:
        layer_list.append(line.split(',')[1])

    for i in range(len(CONFIGS)):
        config_name = str(CONFIGS[i])
        dir_name = os.path.join(SCHEDULE_DIR, NETWORK_NAME, config_name)
        for r, d, f in os.walk(dir_name, topdown=True):
            for filename in sorted(f):
                if filename[-3:] == "csv":
                    fi = open(os.path.join(r, filename), "r")
                    lines = fi.read().splitlines()
                    last_node = ":)"
                    msgs = []
                    cols = None
                    node = None
                    last_cols = None
                    last_layer = None
                    last_layer_short = None
                    last_layer_partition = 'other'
                    last_outputs = []
                    current_outputs = []
                    for j in range(1, len(lines)):
                        line = lines[j]
                        cols = line.split(',')
                        node,layer,order,ifmap,ofmap,fmap = cols
                        ifmap = int(ifmap)
                        ofmap = int(ofmap)
                        fmap  = int(fmap)
                        layer_short = layer.split("_part_")[0]
                        if layer_short==layer_list[0]: # first layer
                            msgs.append([-1,node,ifmap])
                        elif node != last_node or "_i" in layer or "_o" in layer: # changing nodes
                            if layer_short == last_layer_short:  # same layer
                                if '_o' in layer: #input comes from last layer
                                    if last_layer_partition =='output': #last layer -> many chips
                                        for k in range(len(last_outputs)):
                                            msgs.append([last_outputs[k][0],node,last_outputs[k][1]])
                                    else:
                                        msgs.append([last_node,node,ifmap])
                                    current_outputs.append([node,ofmap])
                                else: #input comes from last node
                                    msgs.append([last_node,node,ifmap]) 

                            #changing layers
                            else:
                                last_layer_partition = 'other' if last_layer is None else \
                                                        ('output' if "_o" in last_layer else \
                                                         ('input' if "_i" in last_layer else 'other'))
                                last_outputs = current_outputs
                                current_outputs = []
                                if "_o" in layer: # new layer is output partitioned
                                    current_outputs.append([node,ofmap])

                                if last_layer_partition == 'output': #input comes from last layer, many chips
                                   for k in range(len(last_outputs)):
                                        msgs.append([last_outputs[k][0],node,last_outputs[k][1]])
                                else:
                                    msgs.append([last_node,node,ifmap])
                            
                        if (layer_short==layer_list[-1] and "_o" in layer) or (j==(len(lines)-1)):
                            msgs.append([node, -1, ofmap])


                        if last_layer_short is not None:
                            i0 = layer_list.index(layer_short)
                            i1 = layer_list.index(last_layer_short)
                            assert (i0==i1) or (i0==(i1+1)), ("Mapping out of order error: was  %d and %d!!"%(i0,i1))

                        last_layer = layer
                        last_layer_short = layer_short
                        last_node = node
                        last_cols = cols


                    output_name = net + "_mem_" + str(config_name)

                    N = int(node.split('_')[1])
                    N = math.log(N)/math.log(2)
                    N = math.ceil(N)
                    N = int(math.pow(2,N))

                    visited = [False for k in range(N)]
                    for k in range(len(msgs)):
                        if msgs[k][0] == -1:
                            #msgs[k][0] = "node_%d" %(N-1)
                            msgs[k][0] = N-1
                        else:
                            msgs[k][0] = int(msgs[k][0].split("_")[1]) -1 #0 index
                        visited[msgs[k][0]] = True

                        if msgs[k][1] == -1:
                            #msgs[k][1] = "node_%d" %(N-1)
                            msgs[k][1] = N-1
                        else:
                            msgs[k][1] = int(msgs[k][1].split("_")[1]) -1 #0 index

                        msgs[k][2] = int(msgs[k][2]/16) # 128 bit network

                    for k in range(N):
                        if not visited[k]:
                            msgs.append([k,N-1,0])

                    output[output_name] = (CONFIGS[i],N,msgs)
                    fi.close()

    for node, (cfg, N, msgs) in output.items():
        print(net,node,cfg,N)
        schedule = (str(msgs).replace('[','{').replace(']','}')).replace(" ","")
        

        for topo in ["torus","mesh","fattree"]:
            #for inj in [0.5]:
                for n in [1,2,3,4]:
                    k = int(N**(1.0/n))
                    if (k**n != N): continue
                    route = 'nca' if topo=='fattree' else ('dim_order' if topo=="torus" else 'dor')
                    
                    fname = NETWORK_NAME + "_" + str(cfg) + "_" + topo + "_" + str(k) + "_" + str(n) #+ "_" + str(inj)
                    outfile = open(OUTPUT_DIR + "/" + fname, "w")
                    outfile.write(generate_config(N, schedule, topo, route, k, n, fname))
                    outfile.close()





if __name__ == "__main__":
    ap = argparse.ArgumentParser()

    for net in NETS:
        main(net)
