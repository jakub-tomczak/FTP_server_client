package sample;

import com.sun.xml.internal.ws.policy.privateutil.PolicyUtils;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.scene.control.TextField;
import javafx.scene.control.TreeItem;
import javafx.scene.control.TreeView;
import sun.reflect.generics.tree.Tree;

import java.io.*;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

import static java.lang.Thread.sleep;

public class Connection implements  Runnable {

    public Socket client;
    public OutputStream out;
    public InputStream in;
    public String addr;
    public String message;
    public boolean mutex;
    private int port;
    public  PrintWriter writer;
    public BufferedReader reader;
    public String command;
    public String argument;
    public TreeItem<File> fileToUpload;
    public Socket mainSocket;
    public TreeItemExtended fileToDownload;
    public String destinationFolder;

    public Connection(String addr,String port)
    {
        this.addr=addr;
        this.port=Integer.valueOf(port);
    }
    @Override
    public void run()    {


        try {
            if (command=="CONNECT")
            {
                connect();
            }
            if(command=="LIST")
            {
                message=list();
            }

            if(command=="CWD")
            {
                cwd();
            }
            if(command=="RMD")
            {
                rmd();
            }
            if(command=="MKD")
            {
                mkd();
            }
            if(command=="PASV")
            {
                pasv();
            }
            if(command=="MODE")
            {
                setTransmissionMode();
            }
            if(command=="STOR")
            {
                stor(fileToUpload);

            }
            if(command=="RETR")
            {

                retrPom(fileToDownload);
            }


//
        } catch (Exception e) {

            message="ERROR";
        }
    }


    public void connect () throws IOException {

            client = new Socket(addr, port);
            out = client.getOutputStream();
            in = client.getInputStream();
            writer = new PrintWriter(out, true);
            reader = new BufferedReader(new InputStreamReader(in));
        }


    public String list() throws IOException {
        String serverMessage="";
            String command = "LIST /"+argument;
            writer.println(command);
            serverMessage = reader.readLine();
        return serverMessage;

    }

    public void cwd() throws IOException {
        String serverMessage="";
        String command = "CWD "+argument;
        System.out.println(command);
        writer.println(command);
        serverMessage = reader.readLine();
        System.out.println(serverMessage);
    }

    public void rmd() throws  IOException{
        String command="RMD "+argument;
        writer.println(command);
        message = reader.readLine();
    }

    public void mkd() throws IOException{
        String command="MKD "+argument;
        writer.println(command);
        message = reader.readLine();
    }

    public void pasv() throws IOException{
        String command="PASV";
        writer.println(command);
        message = reader.readLine();
    }

    public void pwd() throws IOException{
        String command="PWD";
        writer.println(command);
        message = reader.readLine();
    }

    public void setTransmissionMode() throws IOException{
        String command="TYPE ";
        if(argument=="ASCII"){
            command+="A";
        }
        else
        {
            command+="I";
        }
        writer.println(command);
        message = reader.readLine();
    }
    public void stor(TreeItem<File> f) throws IOException{
        out = mainSocket.getOutputStream();
        in = mainSocket.getInputStream();
        writer = new PrintWriter(out, true);
        reader = new BufferedReader(new InputStreamReader(in));
        if(f.getValue().isDirectory()==true)
        {
            //stworz folder i wejdź w niego
            argument = getFileName(f);
            mkd();
            cwd();
            f.getChildren().removeAll();
            f.getChildren().clear();
            File[] listOfFiles = f.getValue().listFiles();
            if (listOfFiles != null) {
                for (File file : listOfFiles) {
                    f.getChildren().add(new TreeItem<File>(file));
                }
            }
            for(TreeItem file : f.getChildren())
            {
                stor(file);
            }
            //po przetworzeniu wszystkich dzieci wejdź na serwerze poziom wyżej
            pwd();
            String pom[] = message.split("/");
            String dir="";
            for(int i=0;i<pom.length-1;i++)
            {
                dir+=pom[i]+"/";
            }
            argument = "/";
            cwd();
            argument = dir;
            cwd();
        }
        else
        {

            if(client.isClosed())
            {
                client = new Socket(addr,port);
            }
            command = "STOR " + getFileName(f);
            writer.println(command);
            message = reader.readLine();
            DataOutputStream dos = new DataOutputStream(client.getOutputStream());
            FileInputStream fis = new FileInputStream(f.getValue());
            byte[] buffer = new byte[4096];
            while (fis.read(buffer) > 0) {
                dos.write(buffer);
            }
            fis.close();
            dos.close();
            message = reader.readLine();
        }

    }

    public void retr(TreeItemExtended<String> f) throws IOException {

        if(f.getChildren().size()!=0)
        {
            //jeśli pobieramy folder to utwórz lokalnie folder o takiej samej nazwie
            File folder = new File(destinationFolder+"\\"+getFileName(f));
            folder.mkdir();
            destinationFolder=folder.getAbsolutePath();
            //wejdź do niego a następnie przeszukaj dzieci na serwerze
                argument = getFileName(f);
            String input = list();
                if(input.length()>2)
            {
                String k = "";
                k += (char) 3;
               String files[] = input.split(k);

                f.getChildren().removeAll();
                f.getChildren().clear();

                for (String file : files) {
                    TreeItemExtended<String> treeNode = new TreeItemExtended<>(file);
                    f.getChildren().add(treeNode);
                }
            }
                argument = f.getValue().toString();
                cwd();



            //wywołaj metodę dla wszystkich dzieci
            ObservableList<TreeItemExtended> l = f.getChildren();

            for(TreeItemExtended<String> file: l)
            {
                if(file.getValue()!=null) {
                    retr(file);
                }
            }

            //po przetworzeniu całego folderu wejdź poziom wyżej
            destinationFolder=(getParentDir(destinationFolder));
            pwd();
            String pom[] = message.split("/");
            String dir="";
            for(int i=0;i<pom.length-1;i++)
            {
                dir+=pom[i]+"/";
            }
            argument = "/";
            cwd();
            argument = dir;
            cwd();


        }
        else
        {
            if(client.isClosed())
            {
                client = new Socket(addr,port);
            }
            command = "RETR " + getFileName(f);
            writer.println(command);
            message = reader.readLine();
            String size = f.getSize();
            boolean emptyFile=false;
            //jeśli plik jest pusty lub nie udało się odczytać rozmiaru ustaw domyślny rozmiar
            if(Integer.valueOf(size )==0)
            {
                size="128";
                emptyFile=true;
            }
            String path = destinationFolder+"\\"+getFileName(f);
            File downloadFile = new File(path);
            OutputStream outputStream = new BufferedOutputStream(new FileOutputStream(downloadFile));
            InputStream is = client.getInputStream();

            byte [] mybytearray  = new byte [Integer.valueOf(size)+128];
            int bytesRead = is.read(mybytearray,0,mybytearray.length);
            int current=0;
            current = bytesRead;

            do {
                bytesRead =
                        is.read(mybytearray, current, (mybytearray.length-current));
                if(bytesRead >= 0) current += bytesRead;
            } while(bytesRead > -1);

            if(emptyFile==false) {
                outputStream.write(mybytearray, 0, current);
            }
            outputStream.flush();


            outputStream.close();
            is.close();
            message = reader.readLine();
        }
        }
    public void retrPom(TreeItemExtended t) throws IOException {
        out = mainSocket.getOutputStream();
        in = mainSocket.getInputStream();
        writer = new PrintWriter(out, true);
        reader = new BufferedReader(new InputStreamReader(in));
        String pom[] = argument.split("/");
        String dir="";
        for(int i=0;i<pom.length-1;i++)
        {
            dir+=pom[i]+"/";
        }
        argument = dir;
        cwd();
        retr(t);

    }



    public String getFileName(TreeItem file)
    {
        String path= file.getValue().toString().replace("\\","/");
        System.out.println("aa");
        String pom[]=path.split("/");
        String name = pom[pom.length-1];
        return name;
    }

    public String getParentDir(String file)
    {
        String path= file.replace("\\","/");
        System.out.println("aa");
        String pom[]=path.split("/");
        String dir="";
        for(int i=0;i<pom.length-1;i++)
        {
            dir+=pom[i]+"\\";
        }
        return dir;
    }
}


