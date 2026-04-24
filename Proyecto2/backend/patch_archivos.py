import re

with open('src/file_manager.h', 'r') as f:
    content = f.read()

# For remove
content = re.sub(
    r'std::filesystem::path physicalPath = "archivos" \+ path;',
    r'MountedPart* mp_phys = getLoggedMount();\n        std::string partFolder = mp_phys ? "archivos/" + std::string(mp_phys->id) : "archivos";\n        std::filesystem::path physicalPath = std::filesystem::path(partFolder) / (path.empty() ? "" : path.substr(1));',
    content
)

# For rename
content = re.sub(
    r'std::filesystem::path physicalPath = "archivos" \+ params\.at\("path"\);',
    r'MountedPart* mp_phys = getLoggedMount();\n        std::string partFolder = mp_phys ? "archivos/" + std::string(mp_phys->id) : "archivos";\n        std::string pathStr = params.at("path");\n        std::filesystem::path physicalPath = std::filesystem::path(partFolder) / (pathStr.empty() ? "" : pathStr.substr(1));',
    content
)

# For copy/move destination
content = re.sub(
    r'std::filesystem::path destPath = "archivos" \+ params\.at\("destino"\);',
    r'std::string destStr = params.at("destino");\n        std::filesystem::path destPath = std::filesystem::path(partFolder) / (destStr.empty() ? "" : destStr.substr(1));',
    content
)

with open('src/file_manager.h', 'w') as f:
    f.write(content)

