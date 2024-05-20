OutputFile=(input('Output file:'))

def writejsonsplited(read):
    #read=readtxt(input('choose your file'))
    for i in range(len(read)):
        read[i]+='\n'
    fo=open('wrote.txt','w',encoding='utf-8')
    fo.writelines(read)
    fo.close()
    print('check wrote.txt')
def writejson(keyfile,valuefile):
    fo=open(OutputFile,'w',encoding='utf-8')
    head=['{','"translation":[\n']
    fo.writelines(head)
    fo.close()
    en=open(keyfile,'r',encoding='utf-8')
    ru=open(valuefile,'r',encoding='utf-8')
    r1=en.readlines()
    r2=ru.readlines()
    ru.close();en.close()
    r1=[i.strip('\n')for i in r1]
    r2=[i.strip('\n')for i in r2]
    fo=open(OutputFile,'a',encoding='utf-8')
    for i in zip(r1,r2):        
        fo.write('\t{\n')
        fo.write('\t\t"key": "'+i[0]+'",\n')
        fo.write('\t\t"value": "'+i[1]+'"\n')
        fo.write('\t},\n')
    fo.write(']\n}')
    fo.close()
    print('check '+OutputFile)
if __name__=='__main__':
    writejson(input('checker file:'),input('translate file:'))
        
        
