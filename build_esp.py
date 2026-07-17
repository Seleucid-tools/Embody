#!/usr/bin/env python3
# Hand-build Embody.esp — an ESL-flagged .esp (ESPFE) whose sole job is to register the MCM with MCM Helper.
# One Start-Game-Enabled QUST carrying the MCM_ConfigBase script with String property ModName="Embody".
# Field layouts taken verbatim from xEdit wbDefinitionsTES5.pas (QUST + wbVMADFragmentedQUST + wbScriptEntry).
import struct, sys

def u8(v):  return struct.pack('<B', v)
def s8(v):  return struct.pack('<b', v)
def u16(v): return struct.pack('<H', v)
def s16(v): return struct.pack('<h', v)
def u32(v): return struct.pack('<I', v)
def i32(v): return struct.pack('<i', v)
def u64(v): return struct.pack('<Q', v)
def f32(v): return struct.pack('<f', v)

def zstr(s):   return s.encode('ascii') + b'\x00'          # null-terminated (EDID, CNAM, MAST)
def lenstr(s):                                             # uint16 length prefix + chars, NO null (VMAD strings)
    b = s.encode('ascii'); return u16(len(b)) + b

def sub(sig, data):                                        # subrecord: 4-char sig + uint16 size + data
    assert len(sig) == 4
    return sig.encode('ascii') + u16(len(data)) + data

def record(sig, formid, data, flags=0, formver=44):        # 24-byte record header + data
    return (sig.encode('ascii') + u32(len(data)) + u32(flags) + u32(formid)
            + u32(0) + u16(formver) + u16(0) + data)

def grup_top(label_sig, body):                             # top GRUP: 24-byte header + records
    size = 24 + len(body)
    return (b'GRUP' + u32(size) + label_sig.encode('ascii') + i32(0) + u32(0) + u32(0) + body)

# ---- VMAD (script attachment + empty quest-fragment trailer) ----
vmad  = s16(5) + s16(2) + u16(1)                           # version, objFormat, scriptCount
vmad += lenstr('MCM_ConfigBase') + u8(0) + u16(1)         # script name, Local flag, propertyCount
vmad += lenstr('ModName') + u8(2) + u8(1) + lenstr('Embody')  # prop: name, type=String(2), flags=Edited(1), value
vmad += s8(2) + u16(0) + lenstr('') + u16(0)              # Script Fragments: bindVer, fragCount0, fileName"", aliasCount0

# ---- DNAM (General) ----
dnam  = u16(0x0001) + u8(0) + u8(0) + b'\x00\x00\x00\x00' + u32(0)   # Start Game Enabled; prio0; formver0; unk; type None

# ---- QUST record ----
qdata  = sub('EDID', zstr('Embody_MCM'))
qdata += sub('VMAD', vmad)
qdata += sub('DNAM', dnam)
qdata += sub('NEXT', b'')                                 # required empty marker
qust = record('QUST', 0x01000800, qdata)

# ---- TES4 header (ESL-flagged, masters Skyrim.esm) ----
hedr  = f32(1.70) + i32(1) + u32(0x01000801)              # version, numRecords, nextObjectID
t4    = sub('HEDR', hedr)
t4   += sub('CNAM', zstr('Seleucid'))
t4   += sub('MAST', zstr('Skyrim.esm'))
t4   += sub('DATA', u64(0))
tes4  = record('TES4', 0, t4, flags=0x00000200)          # 0x200 = ESL (light) flag -> ESPFE

blob = tes4 + grup_top('QUST', qust)
open(sys.argv[1], 'wb').write(blob)
print(f"wrote {sys.argv[1]}: {len(blob)} bytes  (VMAD={len(vmad)}  QUSTdata={len(qdata)})")
