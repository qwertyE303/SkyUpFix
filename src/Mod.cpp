#include <BML/IMod.h>
#include <BML/IBML.h>

#ifndef MOD_EXPORT
#define MOD_EXPORT extern "C" __declspec(dllexport)
#endif

#ifdef SKYUPFIX_LEGACY_BML
#define SKYUPFIX_BML m_bml
#else
#define SKYUPFIX_BML m_BML
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>

namespace fs = std::filesystem;

static bool g_applied = false;
static int g_framesAfterStart = -1;
static fs::path g_gameDir;

static std::string SafeName(CKObject *obj) {
    if (!obj)
        return "(null)";
    const char *name = obj->GetName();
    return name ? std::string(name) : std::string("(unnamed)");
}

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool ParseSkyName(const std::string &raw, char &letter) {
    std::string s = ToLower(raw);
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".bmp") == 0)
        s.resize(s.size() - 4);

    const char *suffixes[] = {"_front", "_back", "_left", "_right", "_down"};
    for (const char *suffix : suffixes) {
        const size_t n = std::strlen(suffix);
        if (s.size() <= n || s.compare(s.size() - n, n, suffix) != 0)
            continue;
        const std::string prefix = s.substr(0, s.size() - n);
        if (prefix.size() == 5 && prefix.compare(0, 4, "sky_") == 0) {
            const char c = prefix[4];
            if (c >= 'a' && c <= 'l') {
                letter = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return true;
            }
        }
    }
    return false;
}

static bool SamePos(const VxVector &a, const VxVector &b, float eps = 1.0e-3f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps &&
           std::fabs(a.z - b.z) < eps;
}

static fs::path FindGameDirectory() {
    wchar_t path[MAX_PATH] = {};
    fs::path exeDir;
    if (GetModuleFileNameW(nullptr, path, MAX_PATH))
        exeDir = fs::path(path).parent_path();

    std::vector<fs::path> candidates;
    if (!exeDir.empty()) {
        std::wstring folder = exeDir.filename().wstring();
        std::transform(folder.begin(), folder.end(), folder.begin(), std::towlower);
        if (folder == L"bin")
            candidates.push_back(exeDir.parent_path());
        candidates.push_back(exeDir);
    }
    candidates.push_back(fs::current_path());

    std::error_code ec;
    for (const fs::path &candidate : candidates) {
        if (fs::exists(candidate / "Textures" / "Sky", ec))
            return candidate;
    }
    return exeDir.empty() ? fs::current_path() : exeDir.parent_path();
}

class SkyUpFix final : public IMod {
public:
    explicit SkyUpFix(IBML *bml) : IMod(bml) {}

    const char *GetID() override { return "SkyUpFix"; }
    const char *GetVersion() override { return "0.1.0"; }
    const char *GetName() override { return "SkyUpFix"; }
    const char *GetAuthor() override { return "Entity_303-E3"; }
    const char *GetDescription() override {
        return "Adds Sky_<letter>_Up.bmp as the missing top face of Ballance's sky box.";
    }
    DECLARE_BML_VERSION;

    void OnLoad() override {
        g_gameDir = FindGameDirectory();
    }

    void OnPostLoadLevel() override {
        g_applied = false;
    }

    void OnStartLevel() override {
        g_framesAfterStart = 0;
    }

    void OnPostResetLevel() override {
        g_framesAfterStart = 0;
        g_applied = false;
    }

    void OnPostExitLevel() override {
        g_framesAfterStart = -1;
        g_applied = false;
    }

    void OnProcess() override {
        if (g_framesAfterStart < 0)
            return;
        ++g_framesAfterStart;
        const int frame = g_framesAfterStart;
        if (frame == 1 || frame == 5 || frame == 30 || frame == 60 || frame == 90 ||
            frame == 120 || frame == 180 || frame == 240 || frame == 300) {
            ApplyTopFace();
        }
        if (frame > 300)
            g_framesAfterStart = -1;
    }

private:
    CKMesh *FindSkyMesh() {
        if (CK3dEntity *entity = SKYUPFIX_BML->Get3dEntityByName("TT_Sky_Entity")) {
            if (entity->GetMeshCount() > 0) {
                if (CKMesh *mesh = entity->GetMesh(0))
                    return mesh;
            }
        }

        CKContext *ctx = SKYUPFIX_BML->GetCKContext();
        if (!ctx)
            return nullptr;

        auto dedicated = [](const std::string &raw) {
            return ToLower(raw).find("tt_sky") != std::string::npos;
        };

        CKMesh *best = nullptr;
        int bestId = -1;
        auto consider = [&](CKMesh *mesh, int id) {
            if (!mesh || mesh->IsToBeDeleted())
                return;
            if (!best || id > bestId) {
                best = mesh;
                bestId = id;
            }
        };

        const XObjectPointerArray &entities = ctx->GetObjectListByType(CKCID_3DENTITY, TRUE);
        for (int i = 0; i < entities.Size(); ++i) {
            CK3dEntity *entity = static_cast<CK3dEntity *>(entities[i]);
            if (!entity || entity->IsToBeDeleted())
                continue;
            const bool nameSky = dedicated(SafeName(entity));
            for (int m = 0; m < entity->GetMeshCount(); ++m) {
                CKMesh *mesh = entity->GetMesh(m);
                if (!mesh)
                    continue;
                if (nameSky || dedicated(SafeName(mesh)))
                    consider(mesh, static_cast<int>(entity->GetID()));
            }
        }

        const XObjectPointerArray &meshes = ctx->GetObjectListByType(CKCID_MESH, TRUE);
        for (int i = 0; i < meshes.Size(); ++i) {
            CKMesh *mesh = static_cast<CKMesh *>(meshes[i]);
            if (mesh && dedicated(SafeName(mesh)))
                consider(mesh, static_cast<int>(mesh->GetID()));
        }
        return best;
    }

    std::string DetectLetter(CKMesh *mesh) {
        std::vector<char> letters;
        auto addName = [&](const char *raw) {
            if (!raw)
                return;
            char letter = 0;
            if (ParseSkyName(raw, letter) &&
                std::find(letters.begin(), letters.end(), letter) == letters.end())
                letters.push_back(letter);
        };

        for (int i = 0; i < mesh->GetMaterialCount(); ++i) {
            CKMaterial *mat = mesh->GetMaterial(i);
            if (!mat)
                continue;
            addName(mat->GetName());
            if (CKTexture *tex = mat->GetTexture(0))
                addName(tex->GetName());
        }
        for (int i = 0; i < mesh->GetFaceCount(); ++i) {
            CKMaterial *mat = mesh->GetFaceMaterial(i);
            if (!mat)
                continue;
            addName(mat->GetName());
            if (CKTexture *tex = mat->GetTexture(0))
                addName(tex->GetName());
        }
        if (!letters.empty())
            return std::string(1, letters.front());

        CKContext *ctx = SKYUPFIX_BML->GetCKContext();
        CKLevel *level = ctx ? ctx->GetCurrentLevel() : nullptr;
        const std::string levelName = SafeName(level);
        int number = -1;
        for (char ch : levelName) {
            if (ch >= '0' && ch <= '9')
                number = (number < 0) ? (ch - '0') : (number * 10 + (ch - '0'));
            else if (number >= 0)
                break;
        }
        // 1..13: 1=L 2=E 3=A 4=F 5=C 6=H 7=D 8=G 9=K 10=B 11=J 12=I 13=F
        static const char *table = "xLEAFCHDGKBIJF";
        if (number >= 1 && number <= 13)
            return std::string(1, table[number]);
        return {};
    }

    static void CopyMaterialState(CKMaterial *dst, CKMaterial *src) {
        dst->SetAmbient(src->GetAmbient());
        dst->SetDiffuse(src->GetDiffuse());
        dst->SetSpecular(src->GetSpecular());
        dst->SetEmissive(src->GetEmissive());
        dst->SetPower(src->GetPower());
        dst->SetTextureBlendMode(src->GetTextureBlendMode());
        dst->SetTextureMinMode(src->GetTextureMinMode());
        dst->SetTextureMagMode(src->GetTextureMagMode());
        dst->SetTextureAddressMode(src->GetTextureAddressMode());
        dst->SetTextureBorderColor(src->GetTextureBorderColor());
        dst->SetSourceBlend(src->GetSourceBlend());
        dst->SetDestBlend(src->GetDestBlend());
        dst->EnableZWrite(src->ZWriteEnabled());
        dst->SetZFunc(src->GetZFunc());
        dst->EnableAlphaBlend(src->AlphaBlendEnabled());
        dst->EnableAlphaTest(src->AlphaTestEnabled());
        dst->SetAlphaFunc(src->GetAlphaFunc());
        dst->SetAlphaRef(src->GetAlphaRef());
        dst->SetFillMode(src->GetFillMode());
        dst->SetShadeMode(src->GetShadeMode());
        dst->SetTwoSided(TRUE);
    }

    bool AppendTopFace(CKMesh *mesh, const fs::path &topPath, char letter) {
        if (!mesh || mesh->GetClassID() != CKCID_MESH)
            return false;

        const int vertexCount = mesh->GetVertexCount();
        const int faceCount = mesh->GetFaceCount();
        if (vertexCount < 4 || faceCount < 1)
            return false;

        std::vector<VxVector> positions(static_cast<size_t>(vertexCount));
        float minY = 1.0e30f, maxY = -1.0e30f;
        for (int i = 0; i < vertexCount; ++i) {
            mesh->GetVertexPosition(i, &positions[static_cast<size_t>(i)]);
            minY = std::min(minY, positions[static_cast<size_t>(i)].y);
            maxY = std::max(maxY, positions[static_cast<size_t>(i)].y);
        }

        std::vector<int> top;
        for (int i = 0; i < vertexCount; ++i) {
            const VxVector &p = positions[static_cast<size_t>(i)];
            if (std::fabs(p.y - maxY) > 1.0e-3f)
                continue;
            bool duplicate = false;
            for (int existing : top) {
                if (SamePos(p, positions[static_cast<size_t>(existing)])) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                top.push_back(i);
        }
        if (top.size() != 4)
            return false;

        float cx = 0.0f, cz = 0.0f;
        for (int idx : top) {
            cx += positions[static_cast<size_t>(idx)].x;
            cz += positions[static_cast<size_t>(idx)].z;
        }
        cx /= 4.0f;
        cz /= 4.0f;
        std::sort(top.begin(), top.end(), [&](int a, int b) {
            const VxVector &pa = positions[static_cast<size_t>(a)];
            const VxVector &pb = positions[static_cast<size_t>(b)];
            return std::atan2(pa.z - cz, pa.x - cx) < std::atan2(pb.z - cz, pb.x - cx);
        });

        struct Corner {
            VxVector pos;
            float u = 0.0f;
            float v = 0.0f;
        };
        std::vector<Corner> corners;
        corners.reserve(4);
        for (int idx : top) {
            Corner c;
            c.pos = positions[static_cast<size_t>(idx)];
            corners.push_back(c);
        }

        // Match the Down face's UVs so the new top is the mirror of Down.
        std::vector<int> bottom;
        CKMaterial *downMat = nullptr;
        for (int i = 0; i < mesh->GetMaterialCount(); ++i) {
            CKMaterial *mat = mesh->GetMaterial(i);
            if (!mat)
                continue;
            const std::string matName = ToLower(SafeName(mat));
            std::string texName;
            if (CKTexture *tex = mat->GetTexture(0))
                texName = ToLower(SafeName(tex));
            if (matName.find("_down") != std::string::npos ||
                texName.find("_down") != std::string::npos) {
                downMat = mat;
                break;
            }
        }
        if (downMat) {
            for (int f = 0; f < mesh->GetFaceCount(); ++f) {
                if (mesh->GetFaceMaterial(f) != downMat)
                    continue;
                int a = 0, b = 0, c = 0;
                mesh->GetFaceVertexIndex(f, a, b, c);
                const int indices[3] = {a, b, c};
                for (int idx : indices) {
                    const VxVector &p = positions[static_cast<size_t>(idx)];
                    if (std::fabs(p.y - minY) > 1.0e-3f)
                        continue;
                    bool duplicate = false;
                    for (int existing : bottom) {
                        if (SamePos(p, positions[static_cast<size_t>(existing)])) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate)
                        bottom.push_back(idx);
                }
            }
        }

        bool uvFromBottom = !bottom.empty();
        if (uvFromBottom) {
            for (Corner &c : corners) {
                bool found = false;
                for (int idx : bottom) {
                    const VxVector &p = positions[static_cast<size_t>(idx)];
                    if (std::fabs(p.x - c.pos.x) < 1.0e-3f &&
                        std::fabs(p.z - c.pos.z) < 1.0e-3f) {
                        mesh->GetVertexTextureCoordinates(idx, &c.u, &c.v);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    uvFromBottom = false;
                    break;
                }
            }
        }
        if (!uvFromBottom) {
            float minX = corners[0].pos.x, maxX = corners[0].pos.x;
            float minZ = corners[0].pos.z, maxZ = corners[0].pos.z;
            for (const Corner &c : corners) {
                minX = std::min(minX, c.pos.x);
                maxX = std::max(maxX, c.pos.x);
                minZ = std::min(minZ, c.pos.z);
                maxZ = std::max(maxZ, c.pos.z);
            }
            const float dx = std::max(maxX - minX, 1.0e-6f);
            const float dz = std::max(maxZ - minZ, 1.0e-6f);
            for (Corner &c : corners) {
                c.u = (c.pos.x - minX) / dx;
                c.v = (c.pos.z - minZ) / dz;
            }
        }

        // Community texture packs expect the top rotated 180 degrees:
        // swap front/back and left/right relative to the Down mapping.
        for (Corner &c : corners) {
            c.u = c.u;
            c.v = 1.0f - c.v;
        }

        const VxVector e1 = corners[1].pos - corners[0].pos;
        const VxVector e2 = corners[2].pos - corners[0].pos;
        const VxVector normal(e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
                              e1.x * e2.y - e1.y * e2.x);
        if (normal.y > 0.0f)
            std::reverse(corners.begin(), corners.end());

        CKContext *ctx = SKYUPFIX_BML->GetCKContext();
        if (!ctx)
            return false;
        CKLevel *level = ctx->GetCurrentLevel();

        CKMaterial *sourceMat = nullptr;
        for (int i = 0; i < mesh->GetMaterialCount(); ++i) {
            CKMaterial *mat = mesh->GetMaterial(i);
            if (!mat)
                continue;
            if (!sourceMat)
                sourceMat = mat;
            if (CKTexture *tex = mat->GetTexture(0)) {
                if (ToLower(SafeName(tex)).find("_front") != std::string::npos) {
                    sourceMat = mat;
                    break;
                }
            }
        }
        if (!sourceMat)
            return false;

        CKTexture *topTex = static_cast<CKTexture *>(
            ctx->CreateObject(CKCID_TEXTURE, "SkyUpFix_TopTex", CK_OBJECTCREATION_DYNAMIC));
        if (!topTex)
            return false;
        if (CKTexture *sourceTex = sourceMat->GetTexture(0))
            topTex->SetDesiredVideoFormat(sourceTex->GetDesiredVideoFormat());
        if (!topTex->LoadImage(const_cast<char *>(topPath.string().c_str()))) {
            ctx->DestroyObject(topTex);
            return false;
        }
#ifdef SKYUPFIX_LEGACY_BML
        if (CKRenderContext *renderContext = SKYUPFIX_BML->GetRenderContext())
            topTex->SystemToVideoMemory(renderContext);
#endif

        CKMaterial *topMat = static_cast<CKMaterial *>(
            ctx->CreateObject(CKCID_MATERIAL, "SkyUpFix_TopMat", CK_OBJECTCREATION_DYNAMIC));
        if (!topMat) {
            ctx->DestroyObject(topTex);
            return false;
        }
        CopyMaterialState(topMat, sourceMat);
#ifdef SKYUPFIX_LEGACY_BML
        topMat->SetTexture(topTex);
#else
        topMat->SetTexture(0, topTex);
#endif

        if (level) {
            level->AddObject(topTex);
            level->AddObject(topMat);
        }

        const int baseVertex = mesh->GetVertexCount();
        if (!mesh->SetVertexCount(baseVertex + 4)) {
            if (level) {
                level->RemoveObject(topMat);
                level->RemoveObject(topTex);
            }
            ctx->DestroyObject(topMat);
            ctx->DestroyObject(topTex);
            return false;
        }
        const int baseFace = mesh->GetFaceCount();
        if (!mesh->SetFaceCount(baseFace + 2)) {
            mesh->SetVertexCount(baseVertex);
            if (level) {
                level->RemoveObject(topMat);
                level->RemoveObject(topTex);
            }
            ctx->DestroyObject(topMat);
            ctx->DestroyObject(topTex);
            return false;
        }

        const bool prelit = (mesh->GetLitMode() == VX_PRELITMESH);
        const VxVector down(0.0f, -1.0f, 0.0f);
        for (int i = 0; i < 4; ++i) {
            VxVector p = corners[static_cast<size_t>(i)].pos;
            mesh->SetVertexPosition(baseVertex + i, &p);
            mesh->SetVertexTextureCoordinates(baseVertex + i, corners[static_cast<size_t>(i)].u,
                                              corners[static_cast<size_t>(i)].v);
            if (prelit) {
                // Take the colour of the nearest existing vertex instead of vertex
                // 0.  The retail box does not have to be uniformly prelit; using a
                // single colour for the whole new face leaves a slight tint step
                // along the cube edge even when the texture matches exactly.
                int nearest = 0;
                float best = 3.0e38f;
                for (int v = 0; v < baseVertex; ++v) {
                    const VxVector &q = positions[static_cast<size_t>(v)];
                    const float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
                    const float d = dx * dx + dy * dy + dz * dz;
                    if (d < best) {
                        best = d;
                        nearest = v;
                    }
                }
                mesh->SetVertexColor(baseVertex + i, mesh->GetVertexColor(nearest));
            } else {
                mesh->SetVertexNormal(baseVertex + i, const_cast<VxVector *>(&down));
            }
        }

        mesh->SetFaceVertexIndex(baseFace + 0, baseVertex + 0, baseVertex + 1, baseVertex + 2);
        mesh->SetFaceVertexIndex(baseFace + 1, baseVertex + 0, baseVertex + 2, baseVertex + 3);
        mesh->SetFaceMaterial(baseFace + 0, topMat);
        mesh->SetFaceMaterial(baseFace + 1, topMat);

        mesh->VertexMove();
        mesh->UVChanged();
        if (prelit)
            mesh->ColorChanged();
        else
            mesh->NormalChanged();
        mesh->UnOptimize();
        return true;
    }

    static bool MeshHasTop(CKMesh *mesh) {
        // The vanilla TT Sky mesh has 12 triangles (4 sides + 4 Down fan
        // triangles); this mod appends exactly 2.  Checking the count avoids
        // touching face materials that may already belong to a destroyed
        // level after a restart.
        return mesh && mesh->GetFaceCount() > 12;
    }

    void ApplyTopFace() {
        CKMesh *mesh = FindSkyMesh();
        if (!mesh)
            return;

        // Restarting a level can recreate TT_Sky_Mesh while death/respawn keeps
        // it.  Look at the current mesh instead of trusting the previous
        // level's g_applied flag, so a fresh mesh gets the top again and a
        // reused mesh is not duplicated.
        if (MeshHasTop(mesh)) {
            g_applied = true;
            return;
        }
        g_applied = false;

        const std::string letter = DetectLetter(mesh);
        if (letter.empty())
            return;

        const char c = letter[0];
        if (c < 'A' || c > 'L')
            return;

        const fs::path topPath =
            g_gameDir / "Textures" / "Sky" / (std::string("Sky_") + letter + "_Up.bmp");
        std::error_code ec;
        if (!fs::exists(topPath, ec))
            return;

        if (AppendTopFace(mesh, topPath, c))
            g_applied = true;
    }
};

MOD_EXPORT IMod *BMLEntry(IBML *bml) {
    return new SkyUpFix(bml);
}

MOD_EXPORT void BMLExit(IMod *mod) {
    delete mod;
}
