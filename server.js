/*jslint devel:true, node:true */
/*global process, require, socket */

var fs = require('fs'),
	http = require('http'),
    port = "5130",
    host = "127.0.0.1",
    net = require('net'),
    server = net.createServer(),
    readline = require('readline'),
    current_socket = null,
    ptype = require('./packettype.js'),
    addon = require('bindings')('umnode.node'),
    isDebug = false;

console.log(ptype);
server.maxConnections = 3;

process.on('uncaughtException', function (err) {
    "use strict";
    console.log('uncaughtException => ' + err);
    console.error('uncaughtException: ' + err.stack);
});

var global_image = null,
    global_image_buffer = null;
function create_image(w, h) {
    "use strict";
    /*
    uint32_t cur_samples; // temporary
    uint64_t used_vram, free_vram, total_vram;
    float spp;
    uint32_t tri_cnt, meshes_cnt;
    uint32_t rgb32_cnt, rgb32_max, rgb64_cnt, rgb64_max, grey8_cnt, grey8_max, grey16_cnt, grey16_max;
    */
    var image_bytes = w * h * 4,
        header_bytes = 8 * 3 + 4 + 4 * 2 + 4 * 8 + 4 * 3,
        byte_length = header_bytes + image_bytes;
    
    if (w === 0 || h === 0) {
        return null;
    }
    if (!global_image_buffer) {
        global_image_buffer = new Buffer(byte_length);
    }
    if (!global_image) {
        global_image = new Buffer(image_bytes);
        global_image.fill(0xFF);
        
        global_image_buffer.writeUInt32LE(1, header_bytes - 12); //sample
        global_image_buffer.writeUInt32LE(w, header_bytes - 8);
        global_image_buffer.writeUInt32LE(h, header_bytes - 4);
        global_image.copy(global_image_buffer, header_bytes);
        return global_image_buffer;
    }
    if (image_bytes === global_image_buffer.length) {
        return global_image_buffer;
    }
}

function create_header(buffer, type, bytes) {
    "use strict";
    buffer.writeUInt32LE(type, 0);
    buffer.writeUInt32LE(0, 4);
    buffer.writeUInt32LE(bytes, 8);
    buffer.writeUInt32LE(0, 12);
}

function load_mesh_name(chunk, pos) {
    "use strict";
    var i,
        name_end_pos,
        end_pos;
    if (chunk.length > pos) {
        for (i = pos; true; i = i + 1) {
            if (chunk.readUInt8(i) === 0) {
                name_end_pos = i;
                break;
            }
        }
    }
    end_pos =  name_end_pos + (8 - name_end_pos % 8);
    return { value : chunk.toString('utf8', pos + 1, name_end_pos), endpos : end_pos };
}

function load_mesh_count(chunk, pos) {
    "use strict";
    var mesh_count,
        mesh_count_pos;
    if (chunk.length > pos) {
        mesh_count_pos = pos;
        mesh_count = chunk.readUInt32LE(mesh_count_pos);
    }
    return { value : mesh_count, endpos : mesh_count_pos + 8 };
}

function load_mesh_sizes(chunk, mesh_count) {
    "use strict";
    var i,
        k,
        val,
        sizes_list = [],
        sizes = [],
        buf,
        pos;
    /*
    snd << points_size[i]
        << normals_size[i]
        << points_indices_size[i]
        << normals_indices_size[i]
        << vert_per_poly_size[i]
        << uvs_size[i]
        << uv_indices_size[i]
        << shaders_count[i];
        */
    pos = mesh_count.endpos;
    for (i = 0; i < mesh_count.value; i = i + 1) {
        sizes = [];
        for (k = 0; k < 8; k = k + 1) {
            val = chunk.readUInt32LE(pos);
            sizes.push(val);
            pos = pos + 8;
        }
        sizes_list.push(sizes);
    }
    
    buf = chunk.slice(mesh_count.endpos, pos);
    return { buffer : buf, value : sizes_list, endpos : pos };
}

function load_mesh(chunk, mesh_count, mesh_sizes) {
    "use strict";
    var i,
        x,
        y,
        z,
        k,
        point_size,
        normal_size,
        uv_size,
        pos,
        sizes,
        subdiv_divider,
        general_vis,
        buf;
    
    // vertes, normals, uv
    pos = mesh_sizes.endpos;
    for (i = 0; i < mesh_count.value; i = i + 1) {
        sizes = mesh_sizes.value[i];
        point_size = sizes[0];
        normal_size = sizes[1];
        uv_size = sizes[5];
        if (isDebug) {
            for (k = 0; k < point_size; k = k + 1) {
                x = chunk.readFloatLE(pos);
                y = chunk.readFloatLE(pos + 4);
                z = chunk.readFloatLE(pos + 8);
                //console.log("vert:" + x + ", " + y + ", " + z);
                pos = pos + 12;
            }
            for (k = 0; k < normal_size; k = k + 1) {
                x = chunk.readFloatLE(pos);
                y = chunk.readFloatLE(pos + 4);
                z = chunk.readFloatLE(pos + 8);
                //console.log("normal:" + x + ", " + y + ", " + z);
                pos = pos + 12;
            }
            for (k = 0; k < uv_size; k = k + 1) {
                x = chunk.readFloatLE(pos);
                y = chunk.readFloatLE(pos + 4);
                z = chunk.readFloatLE(pos + 8);
                //console.log("uv:" + x + ", " + y + ", " + z);
                pos = pos + 12;
            }
            subdiv_divider = chunk.readFloatLE(pos);
            pos = pos + 4;
            general_vis = chunk.readFloatLE(pos);
            pos = pos + 4;
        } else {
            pos = pos + 12 * (point_size + normal_size + uv_size) + 8;
        }
    }
    
    buf = chunk.slice(mesh_sizes.endpos, pos);
    return { buffer : buf, endpos : pos };
}
             
function load_mesh_index(chunk, mesh_count, mesh_sizes, mesh) {
    "use strict";
    var i,
        k,
        index,
        val,
        val_list = [],
        pos = mesh.endpos,
        sizes,
        point_index_size,
        vert_per_poly_size,
        normals_indices_size,
        uv_indices_size,
        buf;

    // indexies
    for (i = 0; i < mesh_count.value; i = i + 1) {
        sizes = mesh_sizes.value[i];
        point_index_size = sizes[2];
        vert_per_poly_size = sizes[4];
        normals_indices_size = sizes[3];
        uv_indices_size = sizes[6];
        val_list = [];
        if (isDebug) {
            for (k = 0; k < point_index_size; k = k + 1) {
                index = chunk.readUInt32LE(pos);
                val_list.push(index);
                pos = pos + 4;
            }
            console.log("point index: " + val_list);
            val_list = [];
            for (k = 0; k < vert_per_poly_size; k = k + 1) {
                index = chunk.readUInt32LE(pos);
                val_list.push(index);
                pos = pos + 4;
            }
            console.log("vert_per_poly_size: " + val_list);
            val_list = [];
            // poly mat index
            for (k = 0; k < vert_per_poly_size; k = k + 1) {
                index = chunk.readUInt32LE(pos);
                val_list.push(index);
                pos = pos + 4;
            }
            console.log("poly mat index: " + val_list);
            val_list = [];
            for (k = 0; k < normals_indices_size; k = k + 1) {
                index = chunk.readUInt32LE(pos);
                val_list.push(index);
                pos = pos + 4;
            }
            console.log("normals index: " + val_list);
            val_list = [];
            for (k = 0; k < uv_indices_size; k = k + 1) {
                index = chunk.readUInt32LE(pos);
                val_list.push(index);
                pos = pos + 4;
            }
            console.log("uv index: " + val_list);
            // subdivide
            val = chunk.readUInt32LE(pos);
            console.log("subdivide:" + val);
            pos = pos + 4;
            // cam vis
            val = chunk.readUInt32LE(pos);
            console.log("camera visible:" + val);
            pos = pos + 4;
            // shadow vis
            val = chunk.readUInt32LE(pos);
            console.log("shadow visible:" + val);
            pos = pos + 4;
        } else {
            pos = pos + 4 * (point_index_size 
                             + vert_per_poly_size * 2 
                             + normals_indices_size 
                             + uv_indices_size) + 12;
        }
    }
    
    buf = chunk.slice(mesh.endpos, pos);
    return { buffer : buf, endpos : pos };
}

function load_mesh_shader(chunk, mesh_count, mesh_sizes, mesh, indices) {
    "use strict";
    var i,
        k,
        val,
        pos = indices.endpos;
    
    for (i = 0; i < mesh_count.value; i = i + 1) {
        console.log("");// TODO
    }
}

function wait_for_load(type, chunk, bytes, loading, callback) {
    "use strict";
    if (loading.type === null) {
        loading.max_bytes = chunk.readUInt32LE(8) + 16;
        loading.type = type;
    }
    console.log(loading.pos);
    chunk.copy(loading.buffer, loading.pos);
    loading.pos = loading.pos + chunk.length;
    if (loading.pos >= loading.max_bytes) {
        loading.type = null;
        loading.max_bytes = null;
        loading.pos = 0;
        callback(loading.buffer);
    }
}

function new_loading() {
    "use strict";
    return {
        type : null,
        max_bytes : null,
        pos : 0,
        buffer : new Buffer(67108864)
    };
}

server.on('connection', function (socket) {
    "use strict";
    
    var address = server.address(),
        res = new Buffer(8 * 2),
        res_version = new Buffer(4 * 2),
        loading = new_loading(),
        is_exporting_alembic = false;
    console.log('TCP Server listening');// ' + address.address + ":" + address.port);
    console.log(socket.bufferSize);
    
    socket.on('data', function (chunk) {
        var type = chunk[0];
        console.log(chunk);
        if (type === ptype.DESCRIPTION) {
            create_header(res, type, 8);
            console.log(res);
            socket.write(res);
            
            res_version.writeUInt32LE(5, 0); // Major version
            res_version.writeUInt32LE(2, 4); // Minor version
            console.log(res_version);
            socket.write(res_version);
        } else if (type === ptype.RESET || loading.type === ptype.RESET) {
            wait_for_load(type, chunk, socket.bytesRead, loading, function (loadbuffer) {
                if (loadbuffer.length >= 20) {
                    var a = loadbuffer.readUInt32LE(16),
                        isExportAlembic = loadbuffer.readUInt32LE(16 + 4),
                        fps;
                    
                    if (isExportAlembic) {
                        console.log("Exporting Alembic");
                        fps = loadbuffer.readFloatLE(16 + 8);
                        is_exporting_alembic = true;
                        addon.start_alembic_export(fps);
                    }
                    console.log(a);
                }
                create_header(res, type, 0);
                socket.write(res);
            });
        } else if (type === ptype.START || loading.type === ptype.START) {
            wait_for_load(type, chunk, socket.bytesRead, loading, function (loadbuffer) {
                console.log("start render");
                var w = loadbuffer.readInt32LE(16),
                    h = loadbuffer.readInt32LE(16 + 4),
                    img_type = loadbuffer.readUInt32LE(16 + 8);
                console.log(w);
                console.log(h);
                console.log(img_type);
                create_header(res, type, 0);
                socket.write(res);
            });
        } else if (type === ptype.STOP || loading.type === ptype.STOP) {
            wait_for_load(type, chunk, socket.bytesRead, loading, function (loadbuffer) {
                var fps;
                console.log("stop render");
                
                if (is_exporting_alembic) {
                    fps = loadbuffer.readFloatLE(16);
                    console.log("fps:" + fps);
                    console.log("End Exporting Alembic");
                    addon.end_alembic_export(fps);
                    is_exporting_alembic = false;
                }
                create_header(res, type, 0);
                socket.write(res);
                current_socket.end();
                current_socket = null;
                loading = new_loading();
            });
        } else if (type === ptype.GET_IMAGE || loading.type === ptype.GET_IMAGE) {
            wait_for_load(type, chunk, socket.bytesRead, loading, function (loadbuffer) {
                var bytes = loadbuffer.readUInt32LE(8),
                    ui_type,
                    w,
                    h,
                    image;
                console.log(bytes);
                console.log(loadbuffer.length);
                
                /*
                if (bytes >= 12 && loadbuffer.length >= 24) {
                    ui_type = loadbuffer.readUInt32LE(16);
                    w = loadbuffer.readInt32LE(16 + 4);
                    h = loadbuffer.readInt32LE(16 + 8);
                    console.log(ui_type);
                    console.log(w);
                    console.log(h);
                    console.log("create_image");
                    image = create_image(w, h);
                    
                    if (image) {
                        create_header(res, type, image.length);
                        console.log(res);
                        console.log("writeImage:" + image.length);
                        socket.write(Buffer.concat([res, image]));
                        
                    } else {
                        create_header(res, type, 0);
                        socket.write(res);
                    }
                } else {
                */
                create_header(res, type, 0);
                socket.write(res);
            });
        } else if (type === ptype.LOAD_GLOBAL_MESH || loading.type === ptype.LOAD_GLOBAL_MESH) {
            wait_for_load(type, chunk, socket.bytesRead, loading, function (loadbuffer) {
                var i,
                    mesh_count = 0,
                    name,
                    mesh_sizes,
                    mesh,
                    indices,
                    bytes = loadbuffer.readUInt32LE(8);
                
                if (is_exporting_alembic) {
                    console.log("LOAD_GLOBAL_MESH: " + bytes);
                    name = load_mesh_name(loadbuffer, 16);
                    console.log("Mesh Name:" + name.value);

                    mesh_count = load_mesh_count(loadbuffer, name.endpos);
                    console.log("LOAD_GLOBAL_MESH: mesh_count" + mesh_count.value);

                    mesh_sizes = load_mesh_sizes(loadbuffer, mesh_count);
                    mesh = load_mesh(loadbuffer, mesh_count, mesh_sizes);
                    indices = load_mesh_index(loadbuffer, mesh_count, mesh_sizes, mesh);

                    console.log("mesh_sizes len:" + mesh_sizes.buffer.length);
                    console.log("mesh buffer len:" + mesh.buffer.length);
                    console.log("indices len len:" + indices.buffer.length);
                    addon.export_alembic(mesh_count.value, mesh_sizes.buffer, mesh.buffer, indices.buffer);
                }
                console.log("LOAD_GLOBAL_MESH:" + bytes);
                create_header(res, type, 0);
                socket.write(res);
            });
        } else if (type === ptype.LOAD_LOCAL_MESH) {
            console.log("LOAD_LOCAL_MESH");
            create_header(res, type, 0);
            socket.write(res);
        } else if (type <= ptype.LAST_PACKET_TYPE) {
            console.log("packet type:" + type);
            create_header(res, type, 0);
            socket.write(res);
        }
    });
    socket.on('end', function () {
        if (current_socket) {
            current_socket.end();
            current_socket = null;
            console.log("socket connect END");
        }
    });
    if (current_socket) {
        current_socket.end();
        current_socket = null;
    }
    current_socket = socket;
});

server.on('close', function () {
    "use strict";
    console.log('Server Closed');
});

server.listen(port, host, function () {
    "use strict";
    var addr = server.address();
    console.log('Listening Start on Server - ' + addr.address + ':' + addr.port);
});

var rl = readline.createInterface(process.stdin, process.stdout);
rl.on('SIGINT', function () {
    "use strict";
    if (current_socket) {
        current_socket.end();
    }
    server.close();
    rl.close();
});
