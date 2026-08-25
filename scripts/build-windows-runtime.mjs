import { copyFileSync, existsSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const sourceDir = resolve(projectRoot, "native/windows");
const targetExecutable = resolve(sourceDir, "build/Release/RadiantCursor.Runtime.exe");

function run(command, args) {
  console.log(`> ${command} ${args.join(" ")}`);
  const result = spawnSync(command, args, {
    cwd: projectRoot,
    stdio: "inherit",
    shell: false,
  });

  if (result.error) {
    if (result.error.code === "ENOENT") {
      throw new Error(`Comando não encontrado: ${command}`);
    }
    throw result.error;
  }

  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

let buildDir;
let configureArgs;

if (process.platform === "win32") {
  console.log("Windows detectado: compilando o runtime com o toolchain padrão do Visual Studio.");
  buildDir = resolve(sourceDir, "build/windows-x64");
  configureArgs = [
    "-S",
    sourceDir,
    "-B",
    buildDir,
    "-G",
    "Visual Studio 17 2022",
    "-A",
    "x64",
  ];
} else if (process.platform === "linux") {
  console.log("Linux detectado: fazendo cross-build do runtime Windows com MinGW-w64.");
  buildDir = resolve(sourceDir, "build/linux-mingw-x64");
  configureArgs = [
    "-S",
    sourceDir,
    "-B",
    buildDir,
    "-DCMAKE_SYSTEM_NAME=Windows",
    "-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++",
    "-DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres",
    "-DCMAKE_BUILD_TYPE=Release",
  ];
} else {
  throw new Error(`Cross-build do runtime Windows não suportado em ${process.platform}.`);
}

run("cmake", configureArgs);
run("cmake", ["--build", buildDir, "--config", "Release", "--parallel"]);

const builtExecutable = resolve(buildDir, "Release/RadiantCursor.Runtime.exe");
if (!existsSync(builtExecutable)) {
  throw new Error(`O CMake terminou sem gerar ${builtExecutable}`);
}

mkdirSync(dirname(targetExecutable), { recursive: true });
copyFileSync(builtExecutable, targetExecutable);
console.log(`Runtime Windows disponível em ${targetExecutable}`);
