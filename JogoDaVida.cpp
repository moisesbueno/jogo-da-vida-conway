#include <iostream>
#include <CL/opencl.h>
#include <stdbool.h>
#include "pnm.hpp"

using namespace pnm::literals;
using namespace std;

const char *kernel_source =
    "__kernel void life(                                                     \n"
    "   constant bool*input,                                                 \n"
    "   global bool* output,                                                 \n"
    "   const unsigned int height,                                           \n"
    "   const unsigned int width)                                            \n"
    "{                                                                       \n"
    "   int i = get_global_id(0);                                            \n"
    "   int rowUp = i - width;                                               \n"
    "   int rowDown = i + width;                                             \n"
    "   bool outOfBounds = (i < width);                                      \n"
    "   outOfBounds |= (i > (width * (height-1)));                           \n"
    "   outOfBounds |= (i % width == 0);                                     \n"
    "   outOfBounds |= (i % width == width-1);                               \n"
    "   if (outOfBounds) { output[i] = false; return; }                      \n"
    "   int neighbours = input[rowUp-1] + input[rowUp] + input[rowUp+1];     \n"
    "   neighbours += input[i-1] + input[i+1];                               \n"
    "   neighbours += input[rowDown-1] + input[rowDown] + input[rowDown+1];  \n"
    "   if (neighbours == 3 || (input[i] && neighbours == 2))                \n"
    "       output[i] = true;                                                \n"
    "   else                                                                 \n"
    "       output[i] = false;                                               \n"
    "}                                                                       \n";

void Erro(const char *message)
{
    cout << message<<endl;
    exit(1);
}

cl_kernel createKernelFromSource(cl_device_id device_id, cl_context context,
                                 const char *source, const char *name)
{
    int err;
    // Load the source
    cl_program program = clCreateProgramWithSource(context, 1, &source, NULL,
                                                   &err);
    if (err != CL_SUCCESS)
    {
        Erro("Unable to create program");
    }

    // Compile it.
    err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        size_t len;
        char buffer[2048];
        clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG,
                              sizeof(buffer), buffer, &len);
        fprintf(stderr, "%s\n", buffer);
        Erro("Unable to build program");
    }
    // Load it.
    cl_kernel kernel = clCreateKernel(program, name, &err);
    if (!kernel || err != CL_SUCCESS)
    {
        Erro("Unable to create kernel");
    }
    clReleaseProgram(program);

    return kernel;
}

// The board
static const int largura = 128;
static const int altura = 128;
static const size_t board_size = largura * altura;
static _Bool board[board_size];
// Storage for the board.
static cl_mem input;
static cl_mem output;
// OpenCL state
static cl_command_queue queue;
static cl_kernel kernel;
static cl_device_id device_id;
static cl_context context;

void Imprimir(int iteracoes)
{
    unsigned i = 0;
    char valor;
    for (unsigned y = 0; y < altura; y++)
    {
        for (unsigned x = 0; x < largura; x++)
        {
            valor = board[i++] ? '*' : ' ';
            cout << valor;
        }

        cout << endl;
    }

    cout << endl;

    if (iteracoes != 0)
    {
        pnm::image<pnm::bit_pixel> imagem = pnm::read_pbm_ascii("Imagem128x128-Alterada.pbm");

        int indice = 0;
        int lin = 0;
        int col = 0;

        for (auto i = 0; i < largura; i++)
        {
            for (auto j = 0; j < altura; j++)
            {
                indice = i * largura + j;
                lin = indice / largura;
                col = indice % largura;
                imagem[indice / largura][indice % largura] = board[indice];
            }
        }

        pnm::write("Imagem" + to_string(iteracoes) + "iteracoes.pbm", imagem, pnm::format::ascii);
    }
}

void createQueue(void)
{
    int err;
    err = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_ALL, 1, &device_id, NULL);
    if (err != CL_SUCCESS)
    {
        Erro("Unable to enumerate device IDs");
    }

    context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
    if (!context)
    {
        Erro("Unable to create context");
    }

    queue = clCreateCommandQueue(context, device_id, 0, &err);
    if (!queue)
    {
        Erro("Unable to create command queue");
    }
}

void PrepararKernel(void)
{
    input = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(board), NULL, NULL);

    output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(board), NULL, NULL);

    if (!input || !output)
    {
        Erro("Unable to create buffers");
    }

    int err = 0;
    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &output);
    err |= clSetKernelArg(kernel, 2, sizeof(unsigned int), &altura);
    err |= clSetKernelArg(kernel, 3, sizeof(unsigned int), &largura);
    if (err != CL_SUCCESS)
    {
        Erro("Unable to set arguments");
    }
}

void Jogar(int iterations)
{
    if (iterations == 0)
    {
        return;
    }
    int err;
    size_t workgroup_size;
    err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE,
                                   sizeof(size_t), &workgroup_size, NULL);
    if (err != CL_SUCCESS)
    {
        Erro("Unable to get kernel work-group size");
    }

    // Send the board to the OpenCL stack
    err = clEnqueueWriteBuffer(queue, input, CL_TRUE, 0,
                               sizeof(board), board, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        Erro("Unable to enqueue buffer");
    }

    for (unsigned int i = 0; i < iterations; i++)
    {
        // Run the kernel on every cell in the board
        err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &board_size,
                                     &workgroup_size, 0, NULL, NULL);
        if (err)
        {
            Erro("Unable to enqueue kernel");
        }
        if (i < iterations - 1)
        {
            // Copy the output to the input for the next iteration
            err = clEnqueueCopyBuffer(queue, output, input, 0, 0,
                                      sizeof(board), 0, NULL, NULL);
            if (err)
            {
                Erro("Unable to enqueue copy");
            }
        }
    }

    err = clEnqueueReadBuffer(queue, output, CL_TRUE, 0,
                              sizeof(board), board, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        Erro("Unable to read results");
    }
}

int main(int argc, char **argv)
{
    int indice;
    pnm::image<pnm::bit_pixel> imagem = pnm::read_pbm_ascii("Imagem128x128.pbm");

    for (auto i = 32; i < 96; i++)
        for (auto j = 32; j < 96; j++)
            imagem[i][j] = 1;

    pnm::write("Imagem128x128-Alterada.pbm", imagem, pnm::format::ascii);
    cout << "A imagem tem " << imagem.y_size() << " por " << imagem.x_size() << " pixels (h,w)." << std::endl;

    //Declaramos um vetor do tamanho de 128 * 128
    pnm::bit_pixel vetor[board_size];

    //Transformamos a matriz Bidimensional para a matriz unidimensional

    for (auto i = 0; i < largura; i++)
    {
        for (auto j = 0; j < altura; j++)
        {
            indice = i * largura + j;
            vetor[indice] = imagem[i][j];
        }
    }

    for (unsigned int i = 0; i < board_size; i++)
        board[i] = vetor[i].value;

    createQueue();

    kernel = createKernelFromSource(device_id, context, kernel_source, "life");

    PrepararKernel();
    Imprimir(0);

    int iteracoes = 0;
    do
    {
        cout << "Informe o número de interações (0 para sair) ? ";
        cin >> iteracoes;
        Jogar(iteracoes);
        Imprimir(iteracoes);

    } while (iteracoes > 0);

    clReleaseMemObject(input);
    clReleaseMemObject(output);
    clReleaseKernel(kernel);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);

    return 0;
}
