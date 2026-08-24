import { inflateRawSync } from "node:zlib";

import { ENGINE_LIMITS } from "../../shared/engine/limits";

const LOCAL_SIGNATURE = 0x04034b50;
const CENTRAL_SIGNATURE = 0x02014b50;
const END_SIGNATURE = 0x06054b50;
const MAX_ENTRIES = 96;

let crcTable: Uint32Array | null = null;
function crc32(data: Buffer): number {
  if (!crcTable) {
    crcTable = new Uint32Array(256);
    for (let index = 0; index < 256; index += 1) {
      let value = index;
      for (let bit = 0; bit < 8; bit += 1) value = (value & 1) ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
      crcTable[index] = value >>> 0;
    }
  }
  let crc = 0xffffffff;
  for (const byte of data) crc = crcTable[(crc ^ byte) & 0xff]! ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function safeName(name: string): boolean {
  return name.length > 0 && name.length <= 160 && !name.startsWith("/") && !name.includes("\\") && !name.split("/").includes("..") && !/[\u0000-\u001f\u007f]/.test(name);
}

export function createZip(entries: ReadonlyMap<string, Buffer>): Buffer {
  if (entries.size > MAX_ENTRIES) throw new Error("O bundle contém arquivos demais.");
  const localParts: Buffer[] = [];
  const centralParts: Buffer[] = [];
  let offset = 0;
  for (const [name, data] of entries) {
    if (!safeName(name)) throw new Error(`Caminho inseguro no bundle: ${name}`);
    const encoded = Buffer.from(name, "utf8");
    const checksum = crc32(data);
    const local = Buffer.alloc(30);
    local.writeUInt32LE(LOCAL_SIGNATURE, 0); local.writeUInt16LE(20, 4); local.writeUInt16LE(0x800, 6); local.writeUInt16LE(0, 8);
    local.writeUInt32LE(checksum, 14); local.writeUInt32LE(data.length, 18); local.writeUInt32LE(data.length, 22); local.writeUInt16LE(encoded.length, 26);
    localParts.push(local, encoded, data);
    const central = Buffer.alloc(46);
    central.writeUInt32LE(CENTRAL_SIGNATURE, 0); central.writeUInt16LE(20, 4); central.writeUInt16LE(20, 6); central.writeUInt16LE(0x800, 8); central.writeUInt16LE(0, 10);
    central.writeUInt32LE(checksum, 16); central.writeUInt32LE(data.length, 20); central.writeUInt32LE(data.length, 24); central.writeUInt16LE(encoded.length, 28); central.writeUInt32LE(offset, 42);
    centralParts.push(central, encoded);
    offset += local.length + encoded.length + data.length;
  }
  const centralOffset = offset;
  const centralSize = centralParts.reduce((sum, part) => sum + part.length, 0);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(END_SIGNATURE, 0); end.writeUInt16LE(entries.size, 8); end.writeUInt16LE(entries.size, 10); end.writeUInt32LE(centralSize, 12); end.writeUInt32LE(centralOffset, 16);
  return Buffer.concat([...localParts, ...centralParts, end]);
}

export function readZip(archive: Buffer): Map<string, Buffer> {
  if (archive.length > ENGINE_LIMITS.maxExtractedBundleBytes) throw new Error("Bundle grande demais.");
  let endOffset = -1;
  for (let offset = Math.max(0, archive.length - 65_557); offset <= archive.length - 22; offset += 1) {
    if (archive.readUInt32LE(offset) === END_SIGNATURE) endOffset = offset;
  }
  if (endOffset < 0) throw new Error("Arquivo .radiantcursor não é um ZIP válido.");
  const count = archive.readUInt16LE(endOffset + 10);
  const centralOffset = archive.readUInt32LE(endOffset + 16);
  if (count < 1 || count > MAX_ENTRIES || centralOffset >= archive.length) throw new Error("Índice do bundle inválido.");
  const result = new Map<string, Buffer>();
  let cursor = centralOffset;
  let extracted = 0;
  for (let index = 0; index < count; index += 1) {
    if (cursor + 46 > archive.length || archive.readUInt32LE(cursor) !== CENTRAL_SIGNATURE) throw new Error("Diretório ZIP corrompido.");
    const flags = archive.readUInt16LE(cursor + 8);
    const method = archive.readUInt16LE(cursor + 10);
    const checksum = archive.readUInt32LE(cursor + 16);
    const compressedSize = archive.readUInt32LE(cursor + 20);
    const uncompressedSize = archive.readUInt32LE(cursor + 24);
    const nameLength = archive.readUInt16LE(cursor + 28);
    const extraLength = archive.readUInt16LE(cursor + 30);
    const commentLength = archive.readUInt16LE(cursor + 32);
    const localOffset = archive.readUInt32LE(cursor + 42);
    if ((flags & 1) !== 0 || ![0, 8].includes(method) || uncompressedSize > ENGINE_LIMITS.maxExtractedBundleBytes) throw new Error("Compressão ZIP não suportada ou insegura.");
    const nameEnd = cursor + 46 + nameLength;
    if (nameEnd > archive.length) throw new Error("Nome ZIP truncado.");
    const name = archive.subarray(cursor + 46, nameEnd).toString("utf8");
    if (!safeName(name) || result.has(name)) throw new Error("Caminho duplicado ou inseguro no bundle.");
    if (localOffset + 30 > archive.length || archive.readUInt32LE(localOffset) !== LOCAL_SIGNATURE) throw new Error("Entrada ZIP inválida.");
    const localNameLength = archive.readUInt16LE(localOffset + 26);
    const localExtraLength = archive.readUInt16LE(localOffset + 28);
    const start = localOffset + 30 + localNameLength + localExtraLength;
    const end = start + compressedSize;
    if (end > archive.length) throw new Error("Entrada ZIP truncada.");
    const compressed = archive.subarray(start, end);
    const data = method === 0 ? Buffer.from(compressed) : inflateRawSync(compressed, { maxOutputLength: uncompressedSize });
    if (data.length !== uncompressedSize || crc32(data) !== checksum) throw new Error("Integridade do bundle inválida.");
    extracted += data.length;
    if (extracted > ENGINE_LIMITS.maxExtractedBundleBytes) throw new Error("Bundle extraído excede o limite seguro.");
    result.set(name, data);
    cursor = nameEnd + extraLength + commentLength;
  }
  return result;
}
