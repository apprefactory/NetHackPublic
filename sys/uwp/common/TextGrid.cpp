/* NetHack 3.6	TextGrid.cpp	$NHDT-Date:  $  $NHDT-Branch:  $:$NHDT-Revision:  $ */
/* Copyright (c) Bart House, 2016. */
/* Nethack for the Universal Windows Platform (UWP) */
/* NetHack may be freely redistributed.  See license for details. */
#include <wrl.h>
#include <wrl/client.h>
#include <dxgi1_4.h>
#include <d3d11_3.h>
#include <d2d1_3.h>
#include <d2d1effects_2.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <memory>
#include <agile.h>
#include <concrt.h>

#include "..\content\ShaderStructures.h"
#include "DirectXHelper.h"
#include "TextGrid.h"

#include <memory.h>
#include <assert.h>

/* https://en.wikipedia.org/wiki/ANSI_escape_code */

namespace Nethack
{
    #define RGB_TO_XMFLOAT3(r,g,b) { (float) r / (float) 0xff, (float) g / (float) 0xff, (float) b / (float) 0xff }

    static DirectX::XMFLOAT3 s_colorTable[] = {
            RGB_TO_XMFLOAT3(0x55, 0x55, 0x55), // black
            RGB_TO_XMFLOAT3(0xFF, 0x00, 0x00), // red
            RGB_TO_XMFLOAT3(0x00, 0x80, 0x00), // green
            RGB_TO_XMFLOAT3(205, 133, 63), // brown
            RGB_TO_XMFLOAT3(0x00, 0x00, 0xFF), // blue
            RGB_TO_XMFLOAT3(0xFF, 0x00, 0xFF), // magenta
            RGB_TO_XMFLOAT3(0x00, 0xFF, 0xFF), // cyan
            RGB_TO_XMFLOAT3(0x80, 0x80, 0x80), // gray
            RGB_TO_XMFLOAT3(0xFF, 0xFF, 0xFF), // bright
            RGB_TO_XMFLOAT3(0xFF, 0xA5, 0x00), // orange
            RGB_TO_XMFLOAT3(0x00, 0xFF, 0x00), // bright green
            RGB_TO_XMFLOAT3(0xFF, 0xFF, 0x00), // yellow
            RGB_TO_XMFLOAT3(0x00, 0xC0, 0xFF), // bright blue
            RGB_TO_XMFLOAT3(0xFF, 0x80, 0xFF), // bright magenta
            RGB_TO_XMFLOAT3(0x80, 0xFF, 0xFF), // bright cyan
            RGB_TO_XMFLOAT3(0xFF, 0xFF, 0xFF) // white
    };


    TextGrid::TextGrid(const Int2D & inGridDimensions) :
        m_gridDimensions(inGridDimensions),
        m_loadingComplete(false),
        m_gridScreenCenter(0.0f, 0.0f),
        m_scale(1.0f)
    {
        assert(m_gridDimensions.m_x > 0);
        assert(m_gridDimensions.m_y > 0);

        m_cellCount = m_gridDimensions.m_x * m_gridDimensions.m_y;

        m_cells.resize(m_cellCount);
        
        Clear();

        m_vertexCount = m_cellCount * 6;
        m_vertices = new VertexPositionColor[m_vertexCount];

        // Cursor will not use texturing.  We will use a solid
        // white box with invert destination blending.

        DirectX::XMFLOAT3 whiteColor(1.0, 1.0, 1.0);
        for (int i = 0; i < kCursorVertexCount; i++)
        {
            m_cursorVertices[i].color = whiteColor;
            m_cursorVertices[i].coord.x = 0;
            m_cursorVertices[i].coord.y = 0;
        }

        m_cursor.m_x = 0;
        m_cursor.m_y = 0;
        m_cursorBlink = false;
        m_cursorBlinkTicks = ::GetTickCount64() + kCursorTicks;

    }

    void TextGrid::SetCursor(Int2D & cursor)
    {
        m_cellsLock.AcquireExclusive();

        m_cursor = cursor;

        m_dirty = true;

        m_cellsLock.ReleaseExclusive();
    }

    void TextGrid::SetDeviceResources(void)
    {
        m_deviceResources = DX::DeviceResources::s_deviceResources;

        CreateWindowSizeDependentResources();
        CreateDeviceDependentResources();
    }

    void TextGrid::ScaleAndCenter(const Nethack::IntRect & inRect)
    {
        Nethack::Int2D & glyphPixelDimensions = DX::DeviceResources::s_deviceResources->GetGlyphPixelDimensions();

        int screenPixelWidth = inRect.m_bottomRight.m_x - inRect.m_topLeft.m_x;
        int screenPixelHeight = inRect.m_bottomRight.m_y - inRect.m_topLeft.m_y;

        int gridPixelWidth = m_gridDimensions.m_x * glyphPixelDimensions.m_x;
        int gridPixelHeight = m_gridDimensions.m_y * glyphPixelDimensions.m_y;

        float xScale = (float)screenPixelWidth / (float) gridPixelWidth;
        float yScale = (float)screenPixelHeight / (float) gridPixelHeight;
        float scale = (xScale < yScale) ? xScale : yScale;

        SetScale(scale);

        int gridScreenPixelWidth = gridPixelWidth * scale;
        int gridScreenPixelHeight = gridPixelHeight * scale;

        int extraScreenPixelsWidth = screenPixelWidth - gridScreenPixelWidth;
        int extraScreenPixelHeight = screenPixelHeight - gridScreenPixelHeight;

        Nethack::Int2D gridOffset(inRect.m_topLeft.m_x + (extraScreenPixelsWidth / 2), 
                                  inRect.m_topLeft.m_y + (extraScreenPixelHeight / 2));
        SetGridScreenPixelOffset(gridOffset);
    }


    void TextGrid::SetCenterPosition(const Float2D & inScreenPosition)
    {
        m_gridScreenCenter = inScreenPosition;
        CreateWindowSizeDependentResources();
    }

    void TextGrid::SetScale(float inScale)
    {
        m_scale = inScale;
        CreateWindowSizeDependentResources();
    }

    void TextGrid::SetWidth(float inScreenWidth)
    {
        float gridScreenWidth = m_pixelScreenDimensions.m_x * m_gridPixelDimensions.m_x;
        m_scale = inScreenWidth / gridScreenWidth;

        CalculateScreenValues();
    }

    void TextGrid::SetTopLeft(const Float2D & inScreenTopLeft)
    {
        m_gridScreenCenter += inScreenTopLeft - m_screenRect.m_topLeft;
        CalculateScreenValues();
    }

    void TextGrid::SetGridOffset(const Int2D & inGridOffset)
    {
        // move top left to correct offset
        Float2D screenTopLeft = m_cellScreenDimensions * inGridOffset;
        screenTopLeft.m_x += -1.0f;
        screenTopLeft.m_y = 1.0f - screenTopLeft.m_y;

        m_gridScreenCenter += screenTopLeft - m_screenRect.m_topLeft;
        CalculateScreenValues();
    }

    void TextGrid::SetGridScreenPixelOffset(const Int2D & inGridOffset)
    {
        // move top left to correct offset
        Float2D screenTopLeft = m_pixelScreenDimensions * inGridOffset;
        screenTopLeft.m_x += -1.0f;
        screenTopLeft.m_y = 1.0f - screenTopLeft.m_y;

        m_gridScreenCenter += screenTopLeft - m_screenRect.m_topLeft;
        CalculateScreenValues();
    }

    TextGrid::~TextGrid(void)
    {
        delete[] m_vertices;
    }

    void TextGrid::Render(void)
    {
        int64 ticks = ::GetTickCount64();

        if (m_cursorBlinkTicks < ticks)
        {
            m_cursorBlink = !m_cursorBlink;
            m_cursorBlinkTicks = ticks + kCursorTicks;
            m_dirty = true;
        }

        UpdateVertcies();

        assert(m_deviceResources != nullptr);

        auto context = m_deviceResources->GetD3DDeviceContext();

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        context->IASetInputLayout(m_inputLayout.Get());

        // Attach our vertex shader.
        context->VSSetShader(
            m_vertexShader.Get(),
            nullptr,
            0
            );

        // Attach our pixel shader.
        context->PSSetShader(
            m_pixelShader.Get(),
            nullptr,
            0
            );

        // build new vertex buffer
        m_vertexBuffer.Reset();

        D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };

        vertexBufferData.pSysMem = m_vertices;
        vertexBufferData.SysMemPitch = 0;
        vertexBufferData.SysMemSlicePitch = 0;
        CD3D11_BUFFER_DESC vertexBufferDesc(m_vertexCount * sizeof(VertexPositionColor), D3D11_BIND_VERTEX_BUFFER);

        DX::ThrowIfFailed(
            m_deviceResources->GetD3DDevice()->CreateBuffer(
            &vertexBufferDesc,
            &vertexBufferData,
            &m_vertexBuffer
            )
            );

        // Each vertex is one instance of the VertexPositionColor struct.
        UINT stride = sizeof(VertexPositionColor);
        UINT offset = 0;
        context->IASetVertexBuffers(
            0,
            1,
            m_vertexBuffer.GetAddressOf(),
            &stride,
            &offset
            );

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        auto asciiTextureShaderResourceView = m_deviceResources->GetAsciiTextureShaderResourceView();

        context->PSSetShaderResources(
            0,                          // starting at the first shader resource slot
            1,                          // set one shader resource binding
            &asciiTextureShaderResourceView
            );

        auto asciiTextureSampler = m_deviceResources->GetAsciiTextureSampler();

        context->PSSetSamplers(
            0,                          // starting at the first sampler slot
            1,                          // set one sampler binding
            &asciiTextureSampler
            );

        context->OMSetBlendState(NULL, NULL, 0xffffffff);

        context->Draw(
            m_vertexCount,
            0
            );

        if (m_cursorBlink)
        {
            context->PSSetShader(
                m_solidPixelShader.Get(),
                nullptr,
                0
            );

            D3D11_SUBRESOURCE_DATA cursorVertexBufferData = { 0 };

            cursorVertexBufferData.pSysMem = m_cursorVertices;
            cursorVertexBufferData.SysMemPitch = 0;
            cursorVertexBufferData.SysMemSlicePitch = 0;
            CD3D11_BUFFER_DESC cursorVertexBufferDesc(kCursorVertexCount * sizeof(VertexPositionColor), D3D11_BIND_VERTEX_BUFFER);

            DX::ThrowIfFailed(
                m_deviceResources->GetD3DDevice()->CreateBuffer(
                    &cursorVertexBufferDesc,
                    &cursorVertexBufferData,
                    &m_cursorVertexBuffer
                )
            );

            context->IASetVertexBuffers(
                0,
                1,
                m_cursorVertexBuffer.GetAddressOf(),
                &stride,
                &offset
            );

            context->OMSetBlendState(m_invertDstBlendState.Get(), NULL, 0xffffffff);

            context->Draw(
                kCursorVertexCount,
                0
            );
        }

    }

    void TextGrid::ReleaseDeviceDependentResources(void)
    {
        m_loadingComplete = false;

        m_vertexShader.Reset();
        m_inputLayout.Reset();
        m_pixelShader.Reset();
        m_vertexBuffer.Reset();
    }

    void TextGrid::CreateWindowSizeDependentResources(void)
    {
        assert(m_deviceResources != nullptr);

        m_gridPixelDimensions = m_gridDimensions * m_deviceResources->GetGlyphPixelDimensions();

        Windows::Foundation::Size outputSize = m_deviceResources->GetOutputSize();
        m_screenSize = Int2D((int) outputSize.Width, (int) outputSize.Height);
        m_pixelScreenDimensions = Float2D(2.0f / outputSize.Width, 2.0f / outputSize.Height);

        CalculateScreenValues();
    }

    void TextGrid::CalculateScreenValues(void)
    {
        assert(m_deviceResources != nullptr);

        m_gridScreenDimensions = m_pixelScreenDimensions * m_gridPixelDimensions;
        m_gridScreenDimensions *= m_scale;

        Float2D gridScreenOffset = m_gridScreenDimensions / 2.0f;

        Float2D gridScreenTopLeft = m_gridScreenCenter;
        gridScreenTopLeft.m_x -= gridScreenOffset.m_x;
        gridScreenTopLeft.m_y += gridScreenOffset.m_y;

        Float2D gridScreenBottomRight = m_gridScreenCenter;
        gridScreenBottomRight.m_x += gridScreenOffset.m_x;
        gridScreenBottomRight.m_y -= gridScreenOffset.m_y;

        m_screenRect = FloatRect(gridScreenTopLeft, gridScreenBottomRight);

        m_cellScreenDimensions = m_pixelScreenDimensions * m_deviceResources->GetGlyphPixelDimensions();
        m_cellScreenDimensions *= m_scale;

        m_dirty = true;
    }

    void TextGrid::CreateDeviceDependentResources(void)
    {
        assert(m_deviceResources != nullptr);

        // Load shaders asynchronously.
        auto loadVSTask = DX::ReadDataAsync(L"SampleVertexShader.cso");
        auto loadPSTask = DX::ReadDataAsync(L"SamplePixelShader.cso");
        auto loadSolidPSTask = DX::ReadDataAsync(L"SolidPixelShader.cso");

        // After the vertex shader file is loaded, create the shader and input layout.
        auto createVSTask = loadVSTask.then([this](const std::vector<byte>& fileData) {
            DX::ThrowIfFailed(
                m_deviceResources->GetD3DDevice()->CreateVertexShader(
                &fileData[0],
                fileData.size(),
                nullptr,
                &m_vertexShader
                )
                );

            static const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };

            DX::ThrowIfFailed(
                m_deviceResources->GetD3DDevice()->CreateInputLayout(
                vertexDesc,
                ARRAYSIZE(vertexDesc),
                &fileData[0],
                fileData.size(),
                &m_inputLayout
                )
                );
        });

        // After the pixel shader file is loaded, create the shader and constant buffer.
        auto createPSTask = loadPSTask.then([this](const std::vector<byte>& fileData) {
            DX::ThrowIfFailed(
                m_deviceResources->GetD3DDevice()->CreatePixelShader(
                &fileData[0],
                fileData.size(),
                nullptr,
                &m_pixelShader
                )
                );
        });

        // After the pixel shader file is loaded, create the shader and constant buffer.
        auto createSolidPSTask = loadSolidPSTask.then([this](const std::vector<byte>& fileData) {
            DX::ThrowIfFailed(
                m_deviceResources->GetD3DDevice()->CreatePixelShader(
                    &fileData[0],
                    fileData.size(),
                    nullptr,
                    &m_solidPixelShader
                )
            );
        });

        D3D11_BLEND_DESC invertDstBlendDesc = { 0 };

        invertDstBlendDesc.RenderTarget[0].BlendEnable = TRUE;

        invertDstBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_SUBTRACT;
        invertDstBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        invertDstBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;

        invertDstBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        invertDstBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        invertDstBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;

        invertDstBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBlendState(&invertDstBlendDesc, &m_invertDstBlendState));

        m_loadingComplete = true;

    }

    void TextGrid::UpdateVertcies(void)
    {
        m_cellsLock.AcquireExclusive();

        if (m_dirty)
        {
            VertexPositionColor * v = m_vertices;

            float topScreenY = m_screenRect.m_topLeft.m_y;

            for (int y = 0; y < m_gridDimensions.m_y; y++)
            {
                float bottomScreenY = topScreenY - m_cellScreenDimensions.m_y;

                float leftScreenX = m_screenRect.m_topLeft.m_x;

                for (int x = 0; x < m_gridDimensions.m_x; x++)
                {
                    FloatRect glyphRect;
                    auto const & textCell = m_cells[(y * m_gridDimensions.m_x) + x];
                    unsigned char c = textCell.m_char;
                    m_deviceResources->GetGlyphRect(c, glyphRect);
                    DirectX::XMFLOAT3 color = s_colorTable[(int) textCell.m_color];

                    if (textCell.m_color != TextColor::Gray)
                    {
                        color = s_colorTable[(int)textCell.m_color];
                    }

                    float rightScreenX = leftScreenX + m_cellScreenDimensions.m_x;

                    // top left
                    v->pos.x = leftScreenX; v->pos.y = topScreenY; v->pos.z = 0.0f;
                    v->color = color;
                    v->coord.x = glyphRect.m_topLeft.m_x; v->coord.y = glyphRect.m_topLeft.m_y;
                    v++;

                    // top right
                    v->pos.x = rightScreenX; v->pos.y = topScreenY; v->pos.z = 0.0f;
                    v->color = color;
                    v->coord.x = glyphRect.m_bottomRight.m_x; v->coord.y = glyphRect.m_topLeft.m_y;
                    v++;

                    // bottom right
                    v->pos.x = rightScreenX; v->pos.y = bottomScreenY; v->pos.z = 0.0f;
                    v->color = color;
                    v->coord.x = glyphRect.m_bottomRight.m_x; v->coord.y = glyphRect.m_bottomRight.m_y;
                    v++;

                    // top left
                    v->pos.x = leftScreenX; v->pos.y = topScreenY; v->pos.z = 0.0f;
                    v->color = color;
                    v->coord.x = glyphRect.m_topLeft.m_x; v->coord.y = glyphRect.m_topLeft.m_y;
                    v++;

                    // bottom right
                    v->pos.x = rightScreenX; v->pos.y = bottomScreenY; v->pos.z = 0.0f;
                    v->color = color;
                    v->coord.x = glyphRect.m_bottomRight.m_x; v->coord.y = glyphRect.m_bottomRight.m_y;
                    v++;

                    // bottom left
                    v->pos.x = leftScreenX; v->pos.y = bottomScreenY; v->pos.z = 0.0f;
                    v->color = color;
                    v->coord.x = glyphRect.m_topLeft.m_x; v->coord.y = glyphRect.m_bottomRight.m_y;
                    v++;

                    leftScreenX = rightScreenX;
                }

                topScreenY = bottomScreenY;
            }

            float topCursorY = m_screenRect.m_topLeft.m_y - (m_cursor.m_y * m_cellScreenDimensions.m_y);
            float bottomCursorY = topCursorY - m_cellScreenDimensions.m_y;
            float leftCursorX = m_screenRect.m_topLeft.m_x + (m_cursor.m_x * m_cellScreenDimensions.m_x);
            float rightCursorX = leftCursorX + m_cellScreenDimensions.m_x;

            topCursorY -= (m_cellScreenDimensions.m_y * 3) / 4;

            v = m_cursorVertices;
                
            // top left
            v->pos.x = leftCursorX; v->pos.y = topCursorY; v->pos.z = 0.0f;
            v++;

            // top right
            v->pos.x = rightCursorX; v->pos.y = topCursorY; v->pos.z = 0.0f;
            v++;

            // bottom right
            v->pos.x = rightCursorX; v->pos.y = bottomCursorY; v->pos.z = 0.0f;
            v++;

            // top left
            v->pos.x = leftCursorX; v->pos.y = topCursorY; v->pos.z = 0.0f;
            v++;

            // bottom right
            v->pos.x = rightCursorX; v->pos.y = bottomCursorY; v->pos.z = 0.0f;
            v++;

            // bottom left
            v->pos.x = leftCursorX; v->pos.y = bottomCursorY; v->pos.z = 0.0f;
            v++;

            m_dirty = false;
        }

        m_cellsLock.ReleaseExclusive();
    }

    // Called once per frame, rotates the cube and calculates the model and view matrices.
    void TextGrid::Update(DX::StepTimer const& timer)
    {
        // do nothing
    }

    bool TextGrid::HitTest(const Float2D & inScreenPosition)
    {
        Float2D screenPosition(((2.0f * inScreenPosition.m_x) / m_screenSize.m_x) - 1.0f, 1.0f - ((2.0f * inScreenPosition.m_y) / m_screenSize.m_y));

        if (screenPosition.m_x >= m_screenRect.m_topLeft.m_x && screenPosition.m_x <= m_screenRect.m_bottomRight.m_x &&
            screenPosition.m_y <= m_screenRect.m_topLeft.m_y && screenPosition.m_y >= m_screenRect.m_bottomRight.m_y)
        {
            return true;
        }

        return false;
    }

    bool TextGrid::HitTest(const Float2D & inScreenPixelPosition, Int2D & outGridPosition)
    {
        Float2D screenPosition(((2.0f * inScreenPixelPosition.m_x) / m_screenSize.m_x) - 1.0f, 1.0f - ((2.0f * inScreenPixelPosition.m_y) / m_screenSize.m_y));

        if (screenPosition.m_x >= m_screenRect.m_topLeft.m_x && screenPosition.m_x <= m_screenRect.m_bottomRight.m_x &&
            screenPosition.m_y <= m_screenRect.m_topLeft.m_y && screenPosition.m_y >= m_screenRect.m_bottomRight.m_y)
        {
            outGridPosition.m_x = (int) ((screenPosition.m_x - m_screenRect.m_topLeft.m_x) / m_cellScreenDimensions.m_x);
            outGridPosition.m_y = (int) ((m_screenRect.m_topLeft.m_y - screenPosition.m_y) / m_cellScreenDimensions.m_y);

            assert(outGridPosition.m_x >= 0 && outGridPosition.m_x < this->m_gridDimensions.m_x);
            assert(outGridPosition.m_y >= 0 && outGridPosition.m_y < this->m_gridDimensions.m_y);

            return true;
        }

        return false;
    }

    void TextGrid::Clear(void)
    {
        m_cellsLock.AcquireExclusive();

        TextCell clearCell;

        for (auto & c : m_cells)
            c = clearCell;

        m_cursor = Int2D(0, 0);

        m_dirty = true;

        m_cellsLock.ReleaseExclusive();
    }

    void TextGrid::Scroll(int amount)
    {
        bool takeLock = !m_cellsLock.HasExclusive();
        if (takeLock) m_cellsLock.AcquireExclusive();

        int width = m_gridDimensions.m_x;
        int height = m_gridDimensions.m_y;

        for (int y = 0; y < height - amount; y++)
        {
            ::memcpy(&m_cells[y * width], &m_cells[(y + amount) * width], width * sizeof(m_cells[0]));
        }

        TextCell defaultCell;
        for (int i = (height - amount) * width; i < width * height; i++)
            m_cells[i] = defaultCell;

        m_dirty = true;

        if (takeLock) m_cellsLock.ReleaseExclusive();
    }

    void TextGrid::Put(int x, int y, const TextCell & textCell, int len)
    {
        bool takeLock = !m_cellsLock.HasExclusive();
        if (takeLock) m_cellsLock.AcquireExclusive();

        int offset = (y * m_gridDimensions.m_x) + x;

        while (len-- > 0 && offset < m_cellCount)
        {
            m_cells[offset++] = textCell;
        }

        if (offset == m_cellCount)
        {
            offset--;
        }

        m_cursor.m_y = offset / m_gridDimensions.m_x;
        m_cursor.m_x = offset % m_gridDimensions.m_x;

        m_dirty = true;

        if (takeLock) m_cellsLock.ReleaseExclusive();
    }

    void TextGrid::Put(TextColor color, TextAttribute attribute, char c)
    {
        bool takeLock = !m_cellsLock.HasExclusive();
        if (takeLock) m_cellsLock.AcquireExclusive();

        TextCell textCell(color, attribute, ' ');
        int offset = (m_cursor.m_y * m_gridDimensions.m_x) + m_cursor.m_x;

        assert(offset < m_cellCount);

        if (c == '\b')
        {
            if (offset > 0)
            {
                offset--;
                textCell.m_char = ' ';
                m_cells[offset] = textCell;
            }
        }
        else
        {
            textCell.m_char = c;
            m_cells[offset++] = textCell;
        }

        if (offset == m_cellCount)
        {
            offset--;
        }

        m_cursor.m_y = offset / m_gridDimensions.m_x;
        m_cursor.m_x = offset % m_gridDimensions.m_x;

        m_dirty = true;

        if (takeLock) m_cellsLock.ReleaseExclusive();
    }

    void TextGrid::Putstr(TextColor color, TextAttribute attribute, const char * text)
    {
        bool takeLock = !m_cellsLock.HasExclusive();
        if (takeLock) m_cellsLock.AcquireExclusive();

        const char * cPtr = text;

        while (*cPtr != '\0')
        {
            Put(color, attribute, *cPtr++);
        }

        if (takeLock) m_cellsLock.ReleaseExclusive();
    }

    void TextGrid::Putstr(int x, int y, TextColor color, TextAttribute attribute, const char * text)
    {
        bool takeLock = m_cellsLock.HasExclusive();
        if(takeLock) m_cellsLock.AcquireExclusive();

        m_cursor.m_x = x;
        m_cursor.m_y = y;

        Putstr(color, attribute, text);

        if(takeLock) m_cellsLock.ReleaseExclusive();
    }

}

